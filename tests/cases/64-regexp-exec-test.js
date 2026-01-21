var r = new RegExp("ab");
Io.print(r.test("zzabzz"));
Io.print(r.test("zz"));
var m = r.exec("zzabzz");
Io.print(m[0]);
Io.print(m.index);
Io.print(m.input);

var g = new RegExp("a", "g");
var g1 = g.exec("ba");
Io.print(g1[0]);
Io.print(g.lastIndex);
Io.print(g.exec("ba"));
Io.print(g.lastIndex);

var i = new RegExp("Ab", "i");
Io.print(i.test("zzab"));
