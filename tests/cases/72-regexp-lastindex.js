var r = new RegExp("a", "g");
Io.print((r.lastIndex )+ "\n");
Io.print((r.exec("ba").index )+ "\n");
Io.print((r.lastIndex )+ "\n");
Io.print((r.exec("ba") == null )+ "\n");
Io.print((r.lastIndex )+ "\n");

var r2 = new RegExp("", "g");
var m1 = r2.exec("abc");
Io.print((m1.index )+ "\n");
Io.print((r2.lastIndex )+ "\n");
var m2 = r2.exec("abc");
Io.print((m2.index )+ "\n");
Io.print((r2.lastIndex )+ "\n");
