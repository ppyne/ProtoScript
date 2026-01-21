var o = new Object();
o.a = 1;
Io.print((o.propertyIsEnumerable("a") )+ "\n");
Io.print((o.propertyIsEnumerable("b") )+ "\n");
Io.print((o.propertyIsEnumerable("toString") )+ "\n");
Io.print((o.toLocaleString() )+ "\n");

var d = new Date(0);
Io.print((d.toLocaleString() == d.toLocaleString() )+ "\n");
