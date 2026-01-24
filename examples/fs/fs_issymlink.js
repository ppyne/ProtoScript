var root = Io.tempPath();
Fs.mkdir(root);
var path = root + "/link.txt";
Fs.isSymlink(path);
Fs.rmdir(root);
