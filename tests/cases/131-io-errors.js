try {
    Io.stdin.close();
} catch (e) {
    Io.print((e.name) + "\n");
}

try {
    Io.open("nope.txt", "rw");
} catch (e) {
    Io.print((e.name) + "\n");
}

var path = Io.tempPath();
var f = Io.open(path, "w");
try {
    f.read();
} catch (e) {
    Io.print((e.name) + "\n");
}
f.close();
