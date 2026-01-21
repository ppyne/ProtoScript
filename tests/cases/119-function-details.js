function f(a, b, c) { return a + b + c; }
var g = f.bind(null, 1);
var h = f.bind(null, 1, 2, 3, 4);
Io.print((g.length )+ "\n");
Io.print((h.length )+ "\n");
Io.print((g.name )+ "\n");

function C() {}
Io.print((Object.prototype.propertyIsEnumerable.call(C.prototype, "constructor") )+ "\n");
Io.print((Function.prototype.length )+ "\n");
