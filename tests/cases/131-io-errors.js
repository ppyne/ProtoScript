try {
    Io.close(Io.stdin);
} catch (e) {
    Io.print((e.name) + "\n");
}

var path = Io.tempPath();
var f = Io.open(path, "w");
try {
    Io.read(f);
} catch (e) {
    Io.print((e.name) + "\n");
}
Io.close(f);
