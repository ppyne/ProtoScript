Io.print((JSON.stringify(3)) + "\n");
Io.print((JSON.stringify("a\"b")) + "\n");
Io.print((JSON.stringify([1, undefined, 2])) + "\n");
Io.print((JSON.stringify({a: 1, b: undefined})) + "\n");
Io.print((JSON.stringify(0/0)) + "\n");
var a = [];
a[0] = a;
try {
    JSON.stringify(a);
} catch (e) {
    Io.print((e.name) + "\n");
}
