var root = Io.tempPath();
Fs.mkdir(root);
var path = root + "/my_file.txt";
var f = Io.open(path, "w");
f.write("data");
f.close();
Fs.chmod(path, 0644);
Fs.rm(path);
Fs.rmdir(root);
