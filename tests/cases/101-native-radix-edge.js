try {
  Io.print((10).toString(1.9) + "\n");
} catch (e) {
  Io.print((e.name )+ "\n");
}
Io.print((10).toString(2.1) + "\n");
try {
  Io.print((10).toString(Number("NaN")) + "\n");
} catch (e) {
  Io.print((e.name )+ "\n");
}
try {
  Io.print((10).toString(0) + "\n");
} catch (e) {
  Io.print((e.name )+ "\n");
}
Io.print((0).toString(16) + "\n");
Io.print((Number("NaN")).toString(16) + "\n");
