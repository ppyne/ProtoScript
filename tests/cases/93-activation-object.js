function f(a) {
  return arguments.callee == f && arguments.length == 1;
}
Io.print((f(1) )+ "\n");
