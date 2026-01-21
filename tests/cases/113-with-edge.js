var obj = new Object();
obj.a = 1;
var a = 10;
with (obj) {
  a = 2;
  b = 3;
  Io.print((a )+ "\n");
}
Io.print((a )+ "\n");
Io.print((obj.a )+ "\n");
Io.print((obj.b )+ "\n");
Io.print((b )+ "\n");
var c = 5;
with (obj) {
  Io.print((c )+ "\n");
}
