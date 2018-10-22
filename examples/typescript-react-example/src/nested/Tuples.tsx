/* Typescript file generated by genType. */

// tslint:disable-next-line:no-var-requires
const TuplesBS = require('./Tuples.bs');

export const testTuple: (_1:[number, number]) => number = TuplesBS.testTuple;

// tslint:disable-next-line:interface-over-type-literal
export type coord = [number, number, (null | undefined | number)];

export const origin: [number, number, (null | undefined | number)] = TuplesBS.origin;

export const computeArea: (_1:[number, number, (null | undefined | number)]) => number = function _(Arg1) { const result = TuplesBS.computeArea([Arg1[0], Arg1[1], (Arg1[2] == null ? undefined : Arg1[2])]); return result };

export const computeAreaWithIdent: (_1:coord) => number = function _(Arg1) { const result = TuplesBS.computeAreaWithIdent([Arg1[0], Arg1[1], (Arg1[2] == null ? undefined : Arg1[2])]); return result };

export const computeAreaNoConverters: (_1:[number, number]) => number = TuplesBS.computeAreaNoConverters;

export const coord2d: <T1,T2,T3>(_1:T1, _2:T2) => [T1, T2, (null | undefined | T3)] = TuplesBS.coord2d;

// tslint:disable-next-line:interface-over-type-literal
export type coord2 = [number, number, (null | undefined | number)];

// tslint:disable-next-line:interface-over-type-literal
export type person = {readonly name: string, readonly age: number};

// tslint:disable-next-line:max-classes-per-file 
// tslint:disable-next-line:class-name
export abstract class couple { protected opaque!: any }; /* simulate opaque types */
