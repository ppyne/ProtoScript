var root = Io.tempPath();
Fs.mkdir(root);
var tmp = root + "/tmp";
Fs.mkdir(tmp);

var f = Io.open(root + "/a.log", "w");
f.write("log\n");
f.close();

f = Io.open(root + "/b.txt", "w");
f.write("text\n");
f.close();

var files = Fs.ls(root);
for (file of files) {
    var full = root + "/" + file;
    if (Fs.isFile(full) && file.lastIndexOf(".log") > 0) {
        Fs.mv(full, tmp + "/" + file);
    }
}

var moved = Fs.ls(tmp);
for (file of moved) {
    Fs.rm(tmp + "/" + file);
}

Fs.rm(root + "/b.txt");
Fs.rmdir(tmp);
Fs.rmdir(root);
