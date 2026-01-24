var root = Io.tempPath();
Fs.mkdir(root);
var dir = root + "/tmp";
Fs.mkdir(dir);
Fs.rmdir(dir);
Fs.rmdir(root);
