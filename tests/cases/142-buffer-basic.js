var b = Buffer.alloc(10);
Io.print(Buffer.size(b) + "\n");
Io.print(b[0] + "\n");
b[0] = 0.85;
b[1] = 1052.856;
b[2] = 127.456;
b[3] = 127.689;
b[4] = null;
b[5] = false;
b[6] = true;
b[7] = undefined;
b[8] = "12.7";
b[9] = "nope";
Io.print(b[0] + "\n");
Io.print(b[1] + "\n");
Io.print(b[2] + "\n");
Io.print(b[3] + "\n");
Io.print(b[4] + "\n");
Io.print(b[5] + "\n");
Io.print(b[6] + "\n");
Io.print(b[7] + "\n");
Io.print(b[8] + "\n");
Io.print(b[9] + "\n");
try {
    var x = b[10];
} catch (e) {
    Io.print(e.name + "\n");
}
try {
    b[10] = 1;
} catch (e) {
    Io.print(e.name + "\n");
}
var c = Buffer.slice(b, 1, 2);
Io.print(Buffer.size(c) + "\n");
Io.print(c[0] + "\n");
Io.print(c[1] + "\n");
