var a = Array(3);
 a[2] = 7;
Io.print((a.length )+ "\n");
a.length = 1;
Io.print((a.length )+ "\n");
Io.print((a.toString() )+ "\n");
Io.print((a[2] )+ "\n");
try {
  a.length = -1;
} catch (e) {
  Io.print((e.name )+ "\n");
}
var b = Array(1,2,3);
b.length = 5;
Io.print((b.length )+ "\n");
Io.print((b.toString() )+ "\n");
Io.print((b[4] )+ "\n");
