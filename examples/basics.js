Io.print("=== basics ===");

var a = 2;
var b = 5;
Io.print("a + b = " + (a + b));

function mul(x, y) {
    return x * y;
}

Io.print("mul(3, 4) = " + mul(3, 4));

if (a < b) {
    Io.print("a is smaller");
} else {
    Io.print("b is smaller");
}

var sum = 0;
for (var i = 0; i < 5; i = i + 1) {
    sum = sum + i;
}
Io.print("sum 0..4 = " + sum);

Io.print("=== end basics ===");
