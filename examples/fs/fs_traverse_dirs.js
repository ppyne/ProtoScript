function listDirs(path) {
    var out = [];
    var files = Fs.ls(path, true);
    for (file of files) {
        var full = path + "/" + file;
        if (Fs.isDir(full)) out[out.length] = full;
    }
    return out;
}

var dirs = listDirs(".");
for (d of dirs) Io.print(d + Io.EOL);
