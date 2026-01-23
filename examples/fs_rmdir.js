var root = Io.tempPath();
Fs.mkdir(root);
var path = root + "/example";
Fs.mkdir(path);
if (Fs.isDir(path) && Fs.ls(path).length == 0) {
    Fs.rmdir(path);
}
Fs.rmdir(root);
