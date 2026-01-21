var o = new Object();
o.a = 1;
var keys = "";
for (var k in o) { keys = keys + k; }
Io.print((keys )+ "\n");
