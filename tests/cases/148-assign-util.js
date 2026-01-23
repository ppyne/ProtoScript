include "../../examples/utils/assign.js";

var src = { a: 1, nested: { x: 1 } };
var dst = assign({}, src);
Io.print(dst.a + "\n");
Io.print(dst.nested.x + "\n");

src.nested.x = 2;
Io.print(dst.nested.x + "\n");

var merged = assign({}, null, { b: 2 }, undefined, { c: 3 });
Io.print(merged.b + "\n");
Io.print(merged.c + "\n");

var obj = assign("", { z: 9 });
Io.print(obj.z + "\n");
