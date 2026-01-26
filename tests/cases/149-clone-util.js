ProtoScript.include("../../examples/utils/clone.js");
var src = { a: 1, nested: { x: 1 }, list: [1, 2] };
var copy = clone(src);
Io.print(copy.nested.x + "\n");
Io.print(copy.list[0] + "\n");

src.nested.x = 9;
src.list[0] = 7;
Io.print(copy.nested.x + "\n");
Io.print(copy.list[0] + "\n");

var cycle = { name: "loop" };
cycle.self = cycle;
var c2 = clone(cycle);
Io.print((c2 === c2.self) ? "cycle-ok\n" : "cycle-bad\n");

function Foo() {}
Foo.prototype.tag = "foo";
var inst = new Foo();
var c3 = clone(inst);
Io.print((Object.getPrototypeOf(c3) === Foo.prototype) ? "proto-ok\n" : "proto-bad\n");
