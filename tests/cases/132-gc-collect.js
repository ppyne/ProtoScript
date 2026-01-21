var a = {};
var b = {};
a.next = b;
b.next = a;
a = null;
b = null;
Gc.collect();
var s = Gc.stats();
Io.print((s.freedLast >= 2 ? "OK" : "FAIL") + "\n");
