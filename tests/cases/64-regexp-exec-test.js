var r = new RegExp("ab");
Io.print((r.test("zzabzz") )+ "\n");
Io.print((r.test("zz") )+ "\n");
var m = r.exec("zzabzz");
Io.print((m[0] )+ "\n");
Io.print((m.index )+ "\n");
Io.print((m.input )+ "\n");

var g = new RegExp("a", "g");
var g1 = g.exec("ba");
Io.print((g1[0] )+ "\n");
Io.print((g.lastIndex )+ "\n");
Io.print((g.exec("ba") )+ "\n");
Io.print((g.lastIndex )+ "\n");

var i = new RegExp("Ab", "i");
Io.print((i.test("zzab") )+ "\n");
