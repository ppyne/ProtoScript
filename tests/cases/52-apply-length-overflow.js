var s = new String("hi");
var args = new Object();
args.length = 4294967296;
try {
    String.prototype.slice.apply(s, args);
} catch (e) {
    Io.print((e.name )+ "\n");
}
