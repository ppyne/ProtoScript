var e = new Error("oops");
Io.print((e.name )+ "\n");
Io.print((e.message )+ "\n");
Io.print((e.toString() )+ "\n");

var t = TypeError("bad");
Io.print((t.toString() )+ "\n");

var r = new ReferenceError();
Io.print((r.toString() )+ "\n");

var s = new SyntaxError("x");
Io.print((s.name )+ "\n");
Io.print((s.toString() )+ "\n");

var v = EvalError("no");
Io.print((v.toString() )+ "\n");
