var root = Io.tempPath();
Fs.mkdir(root);
var path = root + "/existing.txt";
var f = Io.open(path, "w");
f.write("ok");
f.close();
Fs.exists(path);
Fs.rm(path);
Fs.rmdir(root);
