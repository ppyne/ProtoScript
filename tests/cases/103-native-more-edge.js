try {
  Array(Number("NaN"));
} catch (e) {
  Io.print((e.name )+ "\n");
}
try {
  Array(1/0);
} catch (e) {
  Io.print((e.name )+ "\n");
}
var a0 = Array(0);
Io.print((a0.length )+ "\n");
Io.print((a0.toString() )+ "\n");
try {
  (10).toString(-1.9);
} catch (e) {
  Io.print((e.name )+ "\n");
}
try {
  (10).toString(1/0);
} catch (e) {
  Io.print((e.name )+ "\n");
}
Io.print((1/0).toString(16) + "\n");
Io.print((Number("NaN")).toString(2) + "\n");
