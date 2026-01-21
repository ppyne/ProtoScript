var r = new RegExp("ab");
var m = r.exec("zzabzz");
Io.print((m[0] )+ "\n");
Io.print((m.index )+ "\n");

var r2 = new RegExp("[a-c]+");
Io.print((r2.exec("zzcab")[0] )+ "\n");
var r3 = new RegExp("[^a]+");
Io.print((r3.exec("bbb")[0] )+ "\n");
var r4 = new RegExp("ab{2,3}c");
Io.print((r4.exec("abbbc")[0] )+ "\n");

var r5 = new RegExp("(cat|dog)");
Io.print((r5.exec("xxdog")[1] )+ "\n");
var r6 = new RegExp("(ab)c\\1");
Io.print((r6.exec("abcab")[0] )+ "\n");

var r7 = new RegExp("^ab");
Io.print((r7.test("abzz") )+ "\n");
Io.print((r7.test("zzab") )+ "\n");
var r8 = new RegExp("ab$");
Io.print((r8.test("zzab") )+ "\n");
Io.print((r8.test("abzz") )+ "\n");

var r9 = new RegExp("\\bword\\b");
var m9 = r9.exec("wordy word");
Io.print((m9[0] )+ "\n");
Io.print((m9.index )+ "\n");

var r10 = new RegExp("abc", "i");
Io.print((r10.test("AbC") )+ "\n");

var r11 = new RegExp("a", "g");
var m11 = r11.exec("baaa");
Io.print((m11[0] )+ "\n");
Io.print((m11.index )+ "\n");
Io.print((r11.lastIndex )+ "\n");
var m12 = r11.exec("baaa");
Io.print((m12[0] )+ "\n");
Io.print((m12.index )+ "\n");
Io.print((r11.lastIndex )+ "\n");
