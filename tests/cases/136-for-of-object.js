var obj = { a: 1, b: 2 };
var sum = 0;
for (var v of obj) {
    sum = sum + v;
}
Io.print(sum + "\n");
