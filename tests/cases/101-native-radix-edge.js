try {
  Io.print((10).toString(1.9));
} catch (e) {
  Io.print(e.name);
}
Io.print((10).toString(2.1));
try {
  Io.print((10).toString(Number("NaN")));
} catch (e) {
  Io.print(e.name);
}
try {
  Io.print((10).toString(0));
} catch (e) {
  Io.print(e.name);
}
Io.print((0).toString(16));
Io.print((Number("NaN")).toString(16));
