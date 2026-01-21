var s = new String("hi");
Io.print((String.prototype.substring.call(s) )+ "\n");
var empty = new Object();
empty.length = 0;
Io.print((String.prototype.substring.apply(s, empty) )+ "\n");
try {
    String.prototype.slice.apply(s, 3);
} catch (e) {
    Io.print((e.name )+ "\n");
}
var bad = new Object();
try {
    String.prototype.slice.apply(s, bad);
} catch (e) {
    Io.print((e.name )+ "\n");
}
var bad2 = new Object();
bad2.length = 1.5;
try {
    String.prototype.slice.apply(s, bad2);
} catch (e) {
    Io.print((e.name )+ "\n");
}
var arr = new Object();
arr[0] = 1;
arr[2] = 3;
arr.length = 3;
function sum(a, b, c) {
    Io.print((a )+ "\n");
    Io.print((b )+ "\n");
    Io.print((c )+ "\n");
}
sum.apply(null, arr);
