var a = new Array(1, 2, 3);
Io.print((a.shift() )+ "\n");
Io.print((a.toString() )+ "\n");
Io.print((a.unshift(9, 8) )+ "\n");
Io.print((a.toString() )+ "\n");
var b = a.slice(1, 3);
Io.print((b.toString() )+ "\n");
var s = new String("hello");
Io.print((s.indexOf("ll") )+ "\n");
Io.print((s.indexOf("x") )+ "\n");
Io.print((s.indexOf("l", 3) )+ "\n");
Io.print((s.substring(1, 4) )+ "\n");
Io.print((s.substring(4, 1) )+ "\n");
var n = new Number(1.234);
Io.print((n.toFixed(2) )+ "\n");
Io.print((n.toFixed(0) )+ "\n");
var f = Boolean.prototype.toString;
try {
    Io.print((f() )+ "\n");
} catch (e) {
    Io.print((e.name )+ "\n");
}
var o = new Object();
Io.print((Object.prototype.isPrototypeOf(o) )+ "\n");
Io.print((o.hasOwnProperty("toString") )+ "\n");
