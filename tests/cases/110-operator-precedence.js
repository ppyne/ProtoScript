Io.print((1 + 2 * 3 )+ "\n");
Io.print((1 + 2) * 3 + "\n");
Io.print((1 << 2 + 1 )+ "\n");
Io.print((((5 & 3) == 1) ? 1 : 0) + "\n");
Io.print(((1 < 2 << 1) ? 1 : 0) + "\n");

var t = 0;
function bump() { t = t + 1; return 1; }
0 && bump();
1 && bump();
1 || bump();
0 || bump();
Io.print((t )+ "\n");

var a, b, c;
a = b = c = 5;
Io.print((a + b + c )+ "\n");

var x = 0;
x = (x = 1, x + 2);
Io.print((x )+ "\n");
