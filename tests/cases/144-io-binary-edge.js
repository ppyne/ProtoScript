var path = Io.tempPath();
var empty = Buffer.alloc(0);
var out = Io.open(path, "wb");
out.write(empty);
out.close();
var inp = Io.open(path, "rb");
var got = inp.read();
inp.close();
Io.print(Buffer.size(got) + "\n");
try {
    Io.open("this_file_should_not_exist.bin", "rb");
} catch (e) {
    Io.print(e.name + "\n");
}
try {
    var f = Io.open(path, "rb");
    f.write("nope");
    f.close();
} catch (e) {
    Io.print(e.name + "\n");
}
