var s = "abc";
Io.print(s.indexOf("b", -1));
Io.print(s.indexOf("", 5));
Io.print(s.substring(3, 1));
Io.print(s.substring(-2, 2));
Io.print(s.slice(-2));
Io.print(s.slice(1, 100));

var a = new Array(1, 2, 3, 4);
Io.print(a.slice(-2).toString());
Io.print(a.slice(2, 1).toString());

var n = new Number(1.4);
Io.print(n.toFixed(-1));

Io.print((new Boolean(false)).toString());
Io.print(Object.prototype.hasOwnProperty.call(1, "toString"));
