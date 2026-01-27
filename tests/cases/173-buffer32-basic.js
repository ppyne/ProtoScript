var b = Buffer.alloc(8);
b[0] = 1;
b[1] = 2;
b[2] = 3;
b[3] = 4;
b[4] = 255;
b[5] = 0;
b[6] = 0;
b[7] = 128;

var v = Buffer32.view(b);
Io.print(Buffer32.size(v) + "\n");
Io.print(Buffer32.byteLength(v) + "\n");
Io.print(v[0] + "\n");
Io.print(v[1] + "\n");

v[0] = 1052.6;
Io.print(b[0] + "," + b[1] + "," + b[2] + "," + b[3] + "\n");

v[1] = 4294967295;
Io.print(b[4] + "," + b[5] + "," + b[6] + "," + b[7] + "\n");

try {
    var x = v[2];
} catch (e) {
    Io.print(e.name + "\n");
}

var a = Buffer32.alloc(1);
Io.print(Buffer32.size(a) + "\n");
a[0] = 1.2;
Io.print(a[0] + "\n");
