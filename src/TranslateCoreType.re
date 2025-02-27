open GenTypeCommon;

open! TranslateTypeExprFromTypes;

let removeOption = (~label: Asttypes.arg_label, coreType: Typedtree.core_type) =>
  switch (coreType.ctyp_desc, label) {
  | (Ttyp_constr(Path.Pident(id), _, [t]), Optional(lbl))
      when Ident.name(id) == "option" =>
    Some((lbl, t))
  | (
      Ttyp_constr(Pdot(Path.Pident(nameSpace), id), _, [t]),
      Optional(lbl),
    )
      /* This has a different representation in 4.03+ */
      when Ident.name(nameSpace) == "FB" && id == "option" =>
    Some((lbl, t))
  | _ => None
  };

type processVariant = {
  noPayloads: list((string, Typedtree.attributes)),
  payloads: list((string, Typedtree.attributes, Typedtree.core_type)),
  unknowns: list(string),
};

let processVariant = (rowFields) => {
  let rec loop = (~noPayloads, ~payloads, ~unknowns, fields: list(Typedtree.row_field)) =>
    switch (fields) {
    | [
      { rf_desc: Typedtree.Ttag(
          {txt: label},
          _,
          /* only variants with no payload */ [],
      ), rf_attributes, _ },
        ...otherFields,
      ] =>
      otherFields
      |> loop(
           ~noPayloads=[(label, rf_attributes), ...noPayloads],
           ~payloads,
           ~unknowns,
         )
    | [{ rf_desc: Ttag({txt: label}, _, [payload]), rf_attributes, _ }, ...otherFields] =>
      otherFields
      |> loop(
           ~noPayloads,
           ~payloads=[(label, rf_attributes, payload), ...payloads],
           ~unknowns,
         )
    | [{ rf_desc: Ttag(_, _, [_, _, ..._]) | Tinherit(_) }, ...otherFields] =>
      otherFields
      |> loop(~noPayloads, ~payloads, ~unknowns=["Tinherit", ...unknowns])
    | [] => {
        noPayloads: noPayloads |> List.rev,
        payloads: payloads |> List.rev,
        unknowns: unknowns |> List.rev,
      }
    };
  rowFields |> loop(~noPayloads=[], ~payloads=[], ~unknowns=[]);
};

let rec translateArrowType =
        (
          ~config,
          ~typeVarsGen,
          ~noFunctionReturnDependencies,
          ~typeEnv,
          ~revArgDeps,
          ~revArgs,
          coreType: Typedtree.core_type,
        ) =>
  switch (coreType.ctyp_desc) {
  | Ttyp_arrow(Nolabel, coreType1, coreType2) =>
    let {dependencies, type_} =
      coreType1 |> translateCoreType_(~config, ~typeVarsGen, ~typeEnv, _);
    let nextRevDeps = List.rev_append(dependencies, revArgDeps);
    coreType2
    |> translateArrowType(
         ~config,
         ~typeVarsGen,
         ~noFunctionReturnDependencies,
         ~typeEnv,
         ~revArgDeps=nextRevDeps,
         ~revArgs=[(Nolabel, type_), ...revArgs],
       );
  | Ttyp_arrow((Labelled(lbl) | Optional(lbl)) as label, coreType1, coreType2) =>
    let asLabel =
      switch (coreType.ctyp_attributes |> Annotation.getGenTypeAsRenaming) {
      | Some(s) => s
      | None => ""
      };
    switch (coreType1 |> removeOption(~label)) {
    | None =>
      let {dependencies, type_: type1} =
        coreType1 |> translateCoreType_(~config, ~typeVarsGen, ~typeEnv);
      let nextRevDeps = List.rev_append(dependencies, revArgDeps);
      coreType2
      |> translateArrowType(
           ~config,
           ~typeVarsGen,
           ~noFunctionReturnDependencies,
           ~typeEnv,
           ~revArgDeps=nextRevDeps,
           ~revArgs=[
             (
               Label(
                 asLabel == "" ? lbl |> Runtime.mangleObjectField : asLabel,
               ),
               type1,
             ),
             ...revArgs,
           ],
         );
    | Some((lbl, t1)) =>
      let {dependencies, type_: type1} =
        t1 |> translateCoreType_(~config, ~typeVarsGen, ~typeEnv);
      let nextRevDeps = List.rev_append(dependencies, revArgDeps);
      coreType2
      |> translateArrowType(
           ~config,
           ~typeVarsGen,
           ~noFunctionReturnDependencies,
           ~typeEnv,
           ~revArgDeps=nextRevDeps,
           ~revArgs=[
             (
               OptLabel(
                 asLabel == "" ? lbl |> Runtime.mangleObjectField : asLabel,
               ),
               type1,
             ),
             ...revArgs,
           ],
         );
    };
  | _ =>
    let {dependencies, type_: retType} =
      coreType |> translateCoreType_(~config, ~typeVarsGen, ~typeEnv);
    let allDeps =
      List.rev_append(
        revArgDeps,
        noFunctionReturnDependencies ? [] : dependencies,
      );

    let labeledConvertableTypes = revArgs |> List.rev;
    let argTypes = labeledConvertableTypes |> NamedArgs.group;

    let functionType =
      Function({
        argTypes,
        componentName: None,
        retType,
        typeVars: [],
        uncurried: false,
      });

    {dependencies: allDeps, type_: functionType};
  }
and translateCoreType_ =
    (
      ~config,
      ~typeVarsGen,
      ~noFunctionReturnDependencies=false,
      ~typeEnv,
      coreType: Typedtree.core_type,
    ) =>
  switch (coreType.ctyp_desc) {
  | Ttyp_alias(ct, _) =>
    ct
    |> translateCoreType_(
         ~config,
         ~typeVarsGen,
         ~noFunctionReturnDependencies=false,
         ~typeEnv,
       )

  | Ttyp_constr(
      Pdot(Pident(id), "t"),
      _,
      [{ctyp_desc: Ttyp_constr(_) | Ttyp_var(_)}],
    ) when Ident.name(id) == "Js" =>
    // Preserve some existing uses of Js.t(Obj.t) and Js.t('a).
    translateObjType(Closed, [])

  | Ttyp_constr(Pdot(Pident(id), "t"), _, [t]) when Ident.name(id) == "Js" =>
    t
    |> translateCoreType_(
         ~config,
         ~typeVarsGen,
         ~noFunctionReturnDependencies=false,
         ~typeEnv,
       )

  | Ttyp_object(tObj, closedFlag) =>
    let getFieldType = (objectField: Typedtree.object_field) =>
      switch (objectField.of_desc) {
      | Typedtree.OTtag({txt: name}, t) => (
          name,
          name |> Runtime.isMutableObjectField
            ? {dependencies: [], type_: ident("")}
            : t |> translateCoreType_(~config, ~typeVarsGen, ~typeEnv),
        )
      | OTinherit(t) => (
          "Inherit",
          t |> translateCoreType_(~config, ~typeVarsGen, ~typeEnv),
        )
      };
    let fieldsTranslations = tObj |> List.map(getFieldType);
    translateObjType(
      closedFlag == Closed ? Closed : Open,
      fieldsTranslations,
    );

  | Ttyp_constr(path, _, typeParams) =>
    let paramsTranslation =
      typeParams |> translateCoreTypes_(~config, ~typeVarsGen, ~typeEnv);
    TranslateTypeExprFromTypes.translateConstr(
      ~config,
      ~paramsTranslation,
      ~path,
      ~typeEnv,
    );

  | Ttyp_poly(_, t) =>
    t
    |> translateCoreType_(
         ~config,
         ~typeVarsGen,
         ~noFunctionReturnDependencies,
         ~typeEnv,
       )

  | Ttyp_arrow(_) =>
    coreType
    |> translateArrowType(
         ~config,
         ~typeVarsGen,
         ~noFunctionReturnDependencies,
         ~typeEnv,
         ~revArgDeps=[],
         ~revArgs=[],
       )

  | Ttyp_tuple(listExp) =>
    let innerTypesTranslation =
      listExp |> translateCoreTypes_(~config, ~typeVarsGen, ~typeEnv);
    let innerTypes = innerTypesTranslation |> List.map(({type_}) => type_);
    let innerTypesDeps =
      innerTypesTranslation
      |> List.map(({dependencies}) => dependencies)
      |> List.concat;

    let tupleType = Tuple(innerTypes);

    {dependencies: innerTypesDeps, type_: tupleType};

  | Ttyp_var(s) => {dependencies: [], type_: TypeVar(s)}

  | Ttyp_variant(rowFields, _, _) =>
    switch (rowFields |> processVariant) {
    | {noPayloads, payloads, unknowns: []} =>
      let bsString =
        coreType.ctyp_attributes
        |> Annotation.hasAttribute(Annotation.tagIsBsString);
      let bsInt =
        coreType.ctyp_attributes
        |> Annotation.hasAttribute(Annotation.tagIsBsInt);
      let lastBsInt = ref(-1);
      let noPayloads =
        noPayloads
        |> List.map(((label, attributes)) => {
             let labelJS =
               if (bsString) {
                 switch (attributes |> Annotation.getBsAsRenaming) {
                 | Some(labelRenamed) => StringLabel(labelRenamed)
                 | None => StringLabel(label)
                 };
               } else if (bsInt) {
                 switch (attributes |> Annotation.getBsAsInt) {
                 | Some(n) =>
                   lastBsInt := n;
                   IntLabel(string_of_int(n));
                 | None =>
                   lastBsInt := lastBsInt^ + 1;
                   IntLabel(string_of_int(lastBsInt^));
                 };
               } else {
                 StringLabel(label);
               };
             {label, labelJS};
           });
      let payloadsTranslations =
        payloads
        |> List.map(((label, attributes, payload)) =>
             (
               label,
               attributes,
               payload |> translateCoreType_(~config, ~typeVarsGen, ~typeEnv),
             )
           );
      let payloads =
        payloadsTranslations
        |> List.map(((label, _attributes, translation)) => {
             {
               case: {
                 label,
                 labelJS: StringLabel(label),
               },
               inlineRecord: false,
               numArgs: 1,
               t: translation.type_,
             }
           });
      let type_ =
        createVariant(
          ~bsStringOrInt=bsString || bsInt,
          ~noPayloads,
          ~payloads,
          ~polymorphic=true,
        );
      let dependencies =
        payloadsTranslations
        |> List.map(((_, _, {dependencies})) => dependencies)
        |> List.concat;
      {dependencies, type_};

    | _ => {dependencies: [], type_: mixedOrUnknown(~config)}
    }

  | Ttyp_package({pack_path, pack_fields}) =>
    switch (typeEnv |> TypeEnv.lookupModuleTypeSignature(~path=pack_path)) {
    | Some((signature, typeEnv)) =>
      let typeEquationsTranslation =
        pack_fields
        |> List.map(((x, t)) =>
             (
               x.Asttypes.txt,
               t |> translateCoreType_(~config, ~typeVarsGen, ~typeEnv),
             )
           );
      let typeEquations =
        typeEquationsTranslation
        |> List.map(((x, translation)) => (x, translation.type_));
      let dependenciesFromTypeEquations =
        typeEquationsTranslation
        |> List.map(((_, translation)) => translation.dependencies)
        |> List.flatten;
      let typeEnv1 = typeEnv |> TypeEnv.addTypeEquations(~typeEquations);
      let (dependenciesFromRecordType, type_) =
        signature.sig_type
        |> signatureToModuleRuntimeRepresentation(
             ~config,
             ~typeVarsGen,
             ~typeEnv=typeEnv1,
           );
      {
        dependencies:
          dependenciesFromTypeEquations @ dependenciesFromRecordType,
        type_,
      };
    | None => {dependencies: [], type_: mixedOrUnknown(~config)}
    }

  | Ttyp_any
  | Ttyp_class(_) => {dependencies: [], type_: mixedOrUnknown(~config)}
  }
and translateCoreTypes_ =
    (~config, ~typeVarsGen, ~typeEnv, typeExprs): list(translation) =>
  typeExprs |> List.map(translateCoreType_(~config, ~typeVarsGen, ~typeEnv));

let translateCoreType =
    (~config, ~noFunctionReturnDependencies=?, ~typeEnv, coreType) => {
  let typeVarsGen = GenIdent.createTypeVarsGen();
  let translation =
    coreType
    |> translateCoreType_(
         ~config,
         ~typeVarsGen,
         ~noFunctionReturnDependencies?,
         ~typeEnv,
       );

  if (Debug.dependencies^) {
    translation.dependencies
    |> List.iter(dep => Log_.item("Dependency: %s\n", dep |> depToString));
  };
  translation;
};
