function f(a, b) {
  var alias = arguments;
  Io.print((a )+ "\n");
  alias[0] = 5;
  Io.print((a )+ "\n");
  a = 9;
  Io.print((arguments[0] )+ "\n");
  alias["0"] = 11;
  Io.print((a )+ "\n");
  arguments[1] = 7;
  Io.print((b )+ "\n");
}
f(1, 2);
