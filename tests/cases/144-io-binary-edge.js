var path = Io.tempPath();
var empty = Buffer.alloc(0);
Io.writeBinary(path, empty);
var got = Io.readBinary(path);
Io.print(Buffer.size(got) + "\n");
try {
    Io.readBinary("this_file_should_not_exist.bin");
} catch (e) {
    Io.print(e.name + "\n");
}
try {
    Io.writeBinary(path, "nope");
} catch (e) {
    Io.print(e.name + "\n");
}
