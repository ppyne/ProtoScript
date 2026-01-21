try {
  (1).toString(1);
} catch (e) {
  Io.print((e.name )+ "\n");
}
try {
  (1).toString(37);
} catch (e) {
  Io.print((e.name )+ "\n");
}
try {
  Array(-1);
} catch (e) {
  Io.print((e.name )+ "\n");
}
try {
  Array(3.5);
} catch (e) {
  Io.print((e.name )+ "\n");
}
try {
  Array(4294967296);
} catch (e) {
  Io.print((e.name )+ "\n");
}
