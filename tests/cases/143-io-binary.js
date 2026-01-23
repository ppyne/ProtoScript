var path = Io.tempPath();
var b = Buffer.alloc(4);
b[0] = 1;
b[1] = 0;
b[2] = 255;
b[3] = 42;
var out = Io.open(path, "wb");
out.write(b);
out.close();
var inp = Io.open(path, "rb");
var b2 = inp.read();
inp.close();
Io.print(Buffer.size(b2) + "\n");
Io.print(b2[0] + "\n");
Io.print(b2[1] + "\n");
Io.print(b2[2] + "\n");
Io.print(b2[3] + "\n");
var f = Io.open(path, "r");
try {
    f.read();
} catch (e) {
    Io.print(e.name + "\n");
}
f.close();
var g = Io.open(path, "w");
try {
    g.write(b);
} catch (e) {
    Io.print(e.name + "\n");
}
g.close();
