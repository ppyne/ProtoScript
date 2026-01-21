function show(a, b) {
    Io.print((this.x )+ "\n");
    Io.print((a )+ "\n");
    Io.print((b )+ "\n");
}
var o = new Object();
o.x = 7;
var g = show.bind(o, 1);
g(2);
var h = show.bind(o);
h(3, 4);
function C(a) {
    this.x = a;
}
function get() {
    return this.x;
}
C.prototype.get = get;
var bx = new Object();
bx.x = 1;
var B = C.bind(bx, 5);
var inst = new B(9);
Io.print((inst.x )+ "\n");
Io.print((inst.get() )+ "\n");
try {
    Function.prototype.bind.call(null, o);
} catch (e) {
    Io.print((e.name )+ "\n");
}
