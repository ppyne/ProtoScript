var root = Io.tempPath();
Fs.mkdir(root);
var path = root + "/somefile.txt";
var f = Io.open(path, "w");
f.write("abc");
f.close();
Fs.size(path);
Fs.rm(path);
Fs.rmdir(root);
