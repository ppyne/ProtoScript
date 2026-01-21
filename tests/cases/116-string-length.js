var s = "hello";
Io.print((s.length )+ "\n");
s.length = 99;
Io.print((s.length )+ "\n");

var o = new String("hi");
Io.print((o.length )+ "\n");
Io.print((delete o.length )+ "\n");
o.length = 1;
Io.print((o.length )+ "\n");
