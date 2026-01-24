Io.print("=== basics ===" + "\n");

var a = 2;
var b = 5;
Io.print("a + b = " + (a + b) + "\n");

function mul(x, y) {
    return x * y;
}

Io.print("mul(3, 4) = " + mul(3, 4) + "\n");

if (a < b) {
    Io.print("a is smaller" + "\n");
} else {
    Io.print("b is smaller" + "\n");
}

var sum = 0;
for (var i = 0; i < 5; i = i + 1) {
    sum = sum + i;
}
Io.print("sum 0..4 = " + sum + "\n");

Io.print("=== end basics ===" + "\n");
