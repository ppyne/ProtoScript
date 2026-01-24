function walkTree(path, prefix = "") {
    if (path.lastIndexOf('/') !== path.length - 1) path += "/";
    var files = Fs.ls(path, true, 200);
    for (file of files) {
        if (file == "." || file == "..") continue;
        if (Fs.isSymlink(path + file)) {
            Io.print(prefix + "+- " + file + "@" + Io.EOL);
            continue;
        }
        var full = path + file;
        if (Fs.isDir(full)) {
            Io.print(prefix + "+- " + file + "/" + Io.EOL);
            walkTree(full, prefix + "|  ");
        } else {
            Io.print(prefix + "+- " + file + Io.EOL);
        }
    }
}

var root = Io.tempPath();
Fs.mkdir(root);
Fs.mkdir(root + "/sub");

var f = Io.open(root + "/a.txt", "w");
f.write("a");
f.close();

f = Io.open(root + "/sub/nested.txt", "w");
f.write("nested");
f.close();

walkTree(root);

Fs.rm(root + "/sub/nested.txt");
Fs.rmdir(root + "/sub");
Fs.rm(root + "/a.txt");
Fs.rmdir(root);
