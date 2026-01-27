ProtoScript.include("../../examples/utils/clone.js");
var base = { a: 1 };
var obj = Object.create(base);
Io.print((Object.getPrototypeOf(obj) === base) ? "proto-ok\n" : "proto-bad\n");

var base2 = { b: 2 };
Io.print((Object.setPrototypeOf(obj, base2) === obj) ? "set-ok\n" : "set-bad\n");
Io.print((Object.getPrototypeOf(obj) === base2) ? "set-proto-ok\n" : "set-proto-bad\n");

var n = Object.create(null);
Io.print((Object.getPrototypeOf(n) === null) ? "null-ok\n" : "null-bad\n");

try {
    Object.setPrototypeOf(base, base);
} catch (e) {
    Io.print(e.name + "\n");
}

try {
    Object.create(base, {});
} catch (e) {
    Io.print(e.name + "\n");
}

function Foo() {}
Foo.prototype.tag = "foo";
var inst = new Foo();
inst.x = 1;
var c = clone(inst);
Io.print((Object.getPrototypeOf(c) === Foo.prototype) ? "clone-proto\n" : "clone-noproto\n");
Io.print(c.tag + "\n");

var inst2 = { arr: [1, 2] };
var c2 = clone(inst2);
inst2.arr[0] = 9;
Io.print(c2.arr[0] + "\n");
