function fib(n) {
    if (n < 2) return n;
    if (n === 2) {
        Gc.collect();
    }
    return fib(n - 1) + fib(n - 2);
}

var sum = 0;
for (var i = 0; i < 20; i++) {
    sum += fib(10);
}

if (sum <= 0) {
    Io.print("bad\n");
} else {
    Io.print("OK\n");
}
