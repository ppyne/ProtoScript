var f = Function("a", "b", "return a + b;");
Io.print((f(2, 3) )+ "\n");

var g = Function("a,b", "return a * b;");
Io.print((g(3, 4) )+ "\n");

var h = Function("return 5;");
Io.print((h() )+ "\n");
