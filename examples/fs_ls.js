var path = "./";
var files = Fs.ls(path);
for (file of files) {
    if (Fs.isDir(path + file)) Io.print("directory: " + file + Io.EOL);
    else Io.print("file: " + file + Io.EOL);
}
