var obj = new Object();
obj.a = 1;
var a = 10;
with (obj) {
  a = 2;
  b = 3;
  Io.print(a);
}
Io.print(a);
Io.print(obj.a);
Io.print(obj.b);
Io.print(b);
var c = 5;
with (obj) {
  Io.print(c);
}
