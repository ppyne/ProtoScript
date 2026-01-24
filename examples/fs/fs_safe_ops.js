var root = Io.tempPath();
Fs.mkdir(root);
var src = root + "/input.txt";
var dst = root + "/output.txt";
var f = Io.open(src, "w");
f.write("data");
f.close();

if (Fs.isFile(src)) {
    if (!Fs.cp(src, dst)) Io.print("copy failed" + Io.EOL);
}

Fs.rm(src);
Fs.rm(dst);
Fs.rmdir(root);
