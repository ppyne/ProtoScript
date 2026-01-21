Io.print("=== arrays ===");

var a = Array(1, 2, 3);
Io.print("a.length = " + a.length);
Io.print("a = " + a.toString());

var b = Array(4, 5);
var c = a.concat(b);
Io.print("concat = " + c.toString());

var tail = c.slice(-2);
Io.print("tail = " + tail.toString());

var total = 0;
for (var i = 0; i < c.length; i = i + 1) {
    total = total + c[i];
}
Io.print("total = " + total);

Io.print("=== end arrays ===");
