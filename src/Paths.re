open GenTypeCommon;

let bsconfig = "bsconfig.json";

let rec findProjectRoot = (~dir) =>
  if (Sys.file_exists(Filename.concat(dir, bsconfig))) {
    dir;
  } else {
    let parent = dir |> Filename.dirname;
    if (parent == dir) {
      prerr_endline(
        "Error: cannot find project root containing " ++ bsconfig ++ ".",
      );
      assert(false);
    } else {
      findProjectRoot(~dir=parent);
    };
  };

let setProjectRoot = () => {
  projectRoot := findProjectRoot(~dir=Sys.getcwd());
  bsbProjectRoot :=
    (
      switch (Sys.getenv_opt("BSB_PROJECT_ROOT")) {
      | None => projectRoot^
      | Some(s) => s
      }
    );
};

let concat = Filename.concat;

/*
 * Handle namespaces in cmt files.
 * E.g. src/Module-Project.cmt becomes src/Module
 */
let handleNamespace = cmt => {
  let cutAfterDash = s =>
    switch (String.index(s, '-')) {
    | n => String.sub(s, 0, n)
    | exception Not_found => s
    };
  let noDir = Filename.basename(cmt) == cmt;
  if (noDir) {
    cmt |> Filename.chop_extension |> cutAfterDash;
  } else {
    let dir = cmt |> Filename.dirname;
    let base =
      cmt |> Filename.basename |> Filename.chop_extension |> cutAfterDash;
    Filename.concat(dir, base);
  };
};

let findNameSpace = cmt => {
  let keepAfterDash = s =>
    switch (String.index(s, '-')) {
    | n => Some(String.sub(s, n + 1, String.length(s) - n - 1))
    | exception Not_found => None
    };
  cmt |> Filename.basename |> Filename.chop_extension |> keepAfterDash;
};

/* Get the output file to be written, relative to the project root. */
let getOutputFileRelative = (~config, cmt) =>
  (cmt |> handleNamespace) ++ EmitType.outputFileSuffix(~config);

/* Get the output file to be written, as an absolute path. */
let getOutputFile = (~config, cmt) =>
  Filename.concat(projectRoot^, getOutputFileRelative(~config, cmt));

let getModuleName = cmt =>
  cmt |> handleNamespace |> Filename.basename |> ModuleName.fromStringUnsafe;

let getCmtFile = cmt => {
  let pathCmt = Filename.concat(Sys.getcwd(), cmt);

  let cmtFile =
    if (Filename.check_suffix(pathCmt, ".cmt")) {
      let pathCmtLowerCase = {
        let dirName = pathCmt |> Filename.dirname;
        let baseName = pathCmt |> Filename.basename;
        Filename.concat(dirName, baseName |> String.uncapitalize_ascii);
      };
      let pathCmti = Filename.chop_extension(pathCmt) ++ ".cmti";
      let pathCmtiLowerCase =
        Filename.chop_extension(pathCmtLowerCase) ++ ".cmti";
      if (Sys.file_exists(pathCmtiLowerCase)) {
        pathCmtiLowerCase;
      } else if (Sys.file_exists(pathCmti)) {
        pathCmti;
      } else if (Sys.file_exists(pathCmtLowerCase)) {
        pathCmtLowerCase;
      } else if (Sys.file_exists(pathCmt)) {
        pathCmt;
      } else {
        "";
      };
    } else {
      "";
    };
  cmtFile;
};

let getBsConfigFile = () => {
  let bsconfig = concat(projectRoot^, "bsconfig.json");
  bsconfig |> Sys.file_exists ? Some(bsconfig) : None;
};

/* Find the relative path from /.../bs/lib
   e.g. /foo/bar/bs/lib/src/Hello.re --> src/Hello.re */
let relativePathFromBsLib = fileName =>
  if (Filename.is_relative(fileName)) {
    fileName;
  } else {
    let rec pathToList = path => {
      let isRoot = path |> Filename.basename == path;
      isRoot
        ? [path]
        : [
          path |> Filename.basename,
          ...path |> Filename.dirname |> pathToList,
        ];
    };
    let rec fromLibBs = (~acc, reversedList) =>
      switch (reversedList) {
      | ["default", "_build", ..._] => acc
      | [dir, ...dirs] => fromLibBs(~acc=[dir, ...acc], dirs)
      | [] => [] /* not found */
      };
    fileName
    |> pathToList
    |> fromLibBs(~acc=[])
    |> (
      l =>
        switch (l) {
        | [] => fileName
        | [root, ...dirs] => dirs |> List.fold_left(concat, root)
        }
    );
  };

let readConfig = (~bsVersion, ~namespace) => {
  setProjectRoot();
  Config.readConfig(~bsVersion, ~getBsConfigFile, ~namespace);
};
