function f(a) {
  var has0 = false;
  var hasLength = false;
  var hasCallee = false;
  for (var k in arguments) {
    if (k == "0") has0 = true;
    if (k == "length") hasLength = true;
    if (k == "callee") hasCallee = true;
  }
  Io.print((has0 )+ "\n");
  Io.print((hasLength )+ "\n");
  Io.print((hasCallee )+ "\n");
  Io.print((delete arguments.length )+ "\n");
  Io.print((delete arguments.callee )+ "\n");
  var before = arguments.callee;
  arguments.callee = 1;
  Io.print((arguments.callee == before )+ "\n");
}

f(1);
