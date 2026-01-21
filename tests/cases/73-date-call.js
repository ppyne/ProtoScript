var s0 = Date(0);
var d0 = new Date(0);
Io.print((s0 )+ "\n");
Io.print((d0.toString() )+ "\n");
Io.print((s0 == d0.toString() )+ "\n");

var s1 = Date("1970-01-02T00:00:00Z");
var d1 = new Date("1970-01-02T00:00:00Z");
Io.print((s1 )+ "\n");
Io.print((d1.toString() )+ "\n");
Io.print((s1 == d1.toString() )+ "\n");

Io.print((Date("invalid") )+ "\n");

var u1 = Date.UTC(99, 0, 1);
var u2 = Date.UTC(1999, 0, 1);
Io.print((u1 == u2 )+ "\n");
var u3 = Date.UTC("nope");
Io.print((u3 != u3 )+ "\n");
