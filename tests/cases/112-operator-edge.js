var x = 1;
x += 2 * 3;
Io.print((x )+ "\n");

var y = 5;
y &= 3;
Io.print((y )+ "\n");

var z = 1;
z <<= 3;
Io.print((z )+ "\n");

var o = new Object();
o.x = 1;
Io.print((o.x++ )+ "\n");
Io.print((o.x )+ "\n");
o.y = 5;
++o.y;
Io.print((o.y )+ "\n");
o.x *= 2;
Io.print((o.x )+ "\n");

var a = 1;
var b = a++ + 2;
Io.print((a )+ "\n");
Io.print((b )+ "\n");

var d = new Object();
d.x = 1;
Io.print((delete d.x )+ "\n");
Io.print((d.x )+ "\n");
