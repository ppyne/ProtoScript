var s = "abc";
Io.print((s.indexOf("b", -1) )+ "\n");
Io.print((s.indexOf("", 5) )+ "\n");
Io.print((s.substring(3, 1) )+ "\n");
Io.print((s.substring(-2, 2) )+ "\n");
Io.print((s.slice(-2) )+ "\n");
Io.print((s.slice(1, 100) )+ "\n");

var a = new Array(1, 2, 3, 4);
Io.print((a.slice(-2).toString() )+ "\n");
Io.print((a.slice(2, 1).toString() )+ "\n");

var n = new Number(1.4);
Io.print((n.toFixed(-1) )+ "\n");

Io.print((new Boolean(false)).toString() + "\n");
Io.print((Object.prototype.hasOwnProperty.call(1, "toString") )+ "\n");
