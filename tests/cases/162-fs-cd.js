var rootPath = "";
var f = Io.open("tests/cases/fs_root.txt", "r");
if (f) {
    rootPath = f.read();
    f.close();
}

if (!rootPath || rootPath.length == 0) {
    Io.print("skip\n");
} else {
    var before = Fs.pwd();
    var ok = Fs.cd(rootPath);
    var after = Fs.pwd();
    Fs.cd(before);
    var rootInfo = Fs.pathInfo(rootPath);
    var afterInfo = Fs.pathInfo(after);
    var sameBase = rootInfo && afterInfo && rootInfo.basename == afterInfo.basename;
    Io.print((ok && sameBase) ? "ok\n" : "fail\n");
}
