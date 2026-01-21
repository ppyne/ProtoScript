Io.print((1 + 2 * 3 )+ "\n");
Io.print((1 + 2) * 3 + "\n");
Io.print((5 << 1 )+ "\n");
Io.print((5 >> 1 )+ "\n");
Io.print((5 >>> 1 )+ "\n");
Io.print((5 & 3) + "," + (5 | 3) + "," + (5 ^ 3) + "\n");
Io.print((0 ? 1 : 2 )+ "\n");
Io.print((1 ? 1 : 2 )+ "\n");
Io.print((0 && 1) + "," + (1 && 2) + "\n");
Io.print((0 || 1) + "," + (1 || 2) + "\n");

var s = 0;
for (var i = 0; i < 3; i = i + 1) {
  s = s + i;
}
Io.print((s )+ "\n");

var t = 0;
while (1) {
  t = t + 1;
  if (t == 2) continue;
  if (t == 3) break;
}
Io.print((t )+ "\n");

var u = 0;
do {
  u = u + 1;
} while (u < 2);
Io.print((u )+ "\n");

var v = 0;
for (var k in new Object()) { v = v + 1; }
Io.print((v )+ "\n");
