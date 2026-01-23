var path = Io.tempPath();
var f = Io.open(path, "w");
f.write("abc");
f.close();

var r = Io.open(path, "r");
Io.print(r.read(2) + "\n");
Io.print(r.read(2) + "\n");
var last = r.read(2);
Io.print((last === Io.EOF) ? "eof\n" : "noeof\n");
r.close();

var path2 = Io.tempPath();
var out = Io.open(path2, "wb");
var buf = Buffer.alloc(3);
buf[0] = 1;
buf[1] = 2;
buf[2] = 3;
out.write(buf);
out.close();

var inp = Io.open(path2, "rb");
var b1 = inp.read(2);
Io.print(Buffer.size(b1) + "\n");
var b2 = inp.read(2);
Io.print(Buffer.size(b2) + "\n");
var b3 = inp.read(2);
Io.print((b3 === Io.EOF) ? "eof\n" : "noeof\n");
inp.close();
