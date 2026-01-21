var r = new RegExp("a", "g");
Io.print(r.lastIndex);
Io.print(r.exec("ba").index);
Io.print(r.lastIndex);
Io.print(r.exec("ba") == null);
Io.print(r.lastIndex);

var r2 = new RegExp("", "g");
var m1 = r2.exec("abc");
Io.print(m1.index);
Io.print(r2.lastIndex);
var m2 = r2.exec("abc");
Io.print(m2.index);
Io.print(r2.lastIndex);
