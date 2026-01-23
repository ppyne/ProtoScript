var b = Buffer.alloc(4);
Io.print(Buffer.size(b) + "\n");
Io.print(b[0] + "\n");
b[0] = 255;
b[1] = 300;
b[2] = -5;
b[3] = 3.7;
Io.print(b[0] + "\n");
Io.print(b[1] + "\n");
Io.print(b[2] + "\n");
Io.print(b[3] + "\n");
try {
    var x = b[4];
} catch (e) {
    Io.print(e.name + "\n");
}
try {
    b[4] = 1;
} catch (e) {
    Io.print(e.name + "\n");
}
var c = Buffer.slice(b, 1, 2);
Io.print(Buffer.size(c) + "\n");
Io.print(c[0] + "\n");
Io.print(c[1] + "\n");
