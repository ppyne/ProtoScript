try {
  throw 1;
} catch (e) {
  Io.print((e )+ "\n");
}

try {
  throw new TypeError("bad");
} catch (e) {
  Io.print((e.name + ":" + e.message )+ "\n");
}

try {
  null.foo;
} catch (e) {
  Io.print((e.name )+ "\n");
}

try {
  var a = new Array(1);
  a.length = 1.5;
} catch (e) {
  Io.print((e.name )+ "\n");
}

try {
  throw new SyntaxError("bad");
} catch (e) {
  Io.print((e.name )+ "\n");
}

try {
  eval("var x = ;");
} catch (e) {
  Io.print((e.name )+ "\n");
}
