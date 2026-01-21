Io.print((0 === -0 )+ "\n");
Io.print((0 == -0 )+ "\n");

var n = Number("NaN");
Io.print((n === n )+ "\n");
Io.print((n == n )+ "\n");

Io.print((null == (void 0) )+ "\n");
Io.print((null === (void 0) )+ "\n");

Io.print(("42" == 42 )+ "\n");
Io.print(("42" === 42 )+ "\n");

Io.print((true == 1 )+ "\n");
Io.print((true === 1 )+ "\n");

var o = new Number(3);
Io.print((o == 3 )+ "\n");
Io.print((o === 3 )+ "\n");

var s = new String("a");
Io.print((s == "a" )+ "\n");
Io.print((s === "a" )+ "\n");
