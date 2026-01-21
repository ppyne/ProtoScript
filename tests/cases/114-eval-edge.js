function f() {
  var x = 1;
  eval("var x = 2;");
  return x;
}
Io.print((f() )+ "\n");

var g = 1;
eval("g = g + 4;");
Io.print((g )+ "\n");

var h = eval("1; 2;");
Io.print((h )+ "\n");

try {
  eval("var x = ;");
} catch (e) {
  Io.print((e.name )+ "\n");
}
