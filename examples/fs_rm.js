var root = Io.tempPath();
Fs.mkdir(root);
var filename = root + "/temp.txt";
var f = Io.open(filename, "w");
f.write("tmp");
f.close();
if (Fs.exists(filename)) Fs.rm(filename);
Fs.rmdir(root);
