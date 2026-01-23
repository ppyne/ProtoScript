var path = Io.tempPath();
var b = Buffer.alloc(4);
b[0] = 1;
b[1] = 0;
b[2] = 255;
b[3] = 42;
Io.writeBinary(path, b);
var b2 = Io.readBinary(path);
Io.print(Buffer.size(b2) + "\n");
Io.print(b2[0] + "\n");
Io.print(b2[1] + "\n");
Io.print(b2[2] + "\n");
Io.print(b2[3] + "\n");
var f = Io.open(path, "r");
try {
    Io.read(f);
} catch (e) {
    Io.print(e.name + "\n");
}
Io.close(f);
var g = Io.open(path, "w");
try {
    Io.write(g, b);
} catch (e) {
    Io.print(e.name + "\n");
}
Io.close(g);
