try {
    String.prototype.toString.call(null);
} catch (e) {
    Io.print((e.name )+ "\n");
}
try {
    Number.prototype.valueOf.call(void 0);
} catch (e) {
    Io.print((e.name )+ "\n");
}
try {
    Array.prototype.push.call(null, 1);
} catch (e) {
    Io.print((e.name )+ "\n");
}
var s = new String("hi");
Io.print((String.prototype.substring.call(s, 0, 1) )+ "\n");
var args = new Object();
args[0] = 1;
args[1] = 2;
args.length = 2;
Io.print((String.prototype.slice.apply(s, args) )+ "\n");
var a = new Array();
Array.prototype.push.call(a, 1, 2);
Io.print((a.toString() )+ "\n");
var b = new Array(1);
var o = new Object();
o[0] = "x";
o.length = 1;
var c = Array.prototype.concat.call(b, o);
Io.print((c.toString() )+ "\n");
