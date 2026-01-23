var f = Io.open("tests/cases/fs_root.txt", "r");
var root = f.read();
f.close();

function join(a, b) {
    return a + "/" + b;
}

Io.print("mkdir-existing:" + Fs.mkdir(root) + "\n");
Io.print("mkdir-missing-parent:" + Fs.mkdir(join(root, "nope/child")) + "\n");

Io.print("chmod-missing:" + Fs.chmod(join(root, "missing.txt"), 0644) + "\n");
Io.print("chmod-badmode:" + Fs.chmod(join(root, "file.txt"), "bad") + "\n");

Io.print("cp-missing:" + Fs.cp(join(root, "missing.txt"), join(root, "copy.txt")) + "\n");
Io.print("cp-from-dir:" + Fs.cp(join(root, "empty_dir"), join(root, "copy2.txt")) + "\n");
Io.print("cp-to-dir:" + Fs.cp(join(root, "file.txt"), join(root, "empty_dir")) + "\n");

Io.print("mv-missing:" + Fs.mv(join(root, "missing.txt"), join(root, "new.txt")) + "\n");
Io.print("mv-dir:" + Fs.mv(join(root, "empty_dir"), join(root, "newdir")) + "\n");

Io.print("rm-dir:" + Fs.rm(join(root, "empty_dir")) + "\n");
Io.print("rm-missing:" + Fs.rm(join(root, "missing.txt")) + "\n");

Io.print("rmdir-nonempty:" + Fs.rmdir(join(root, "non_empty_dir")) + "\n");
Io.print("rmdir-file:" + Fs.rmdir(join(root, "file.txt")) + "\n");

Io.print("ls-file:" + Fs.ls(join(root, "file.txt")).length + "\n");
Io.print("ls-missing:" + Fs.ls(join(root, "missing.txt")).length + "\n");

Io.print("readable-missing:" + Fs.isReadable(join(root, "missing.txt")) + "\n");
Io.print("writable-missing:" + Fs.isWritable(join(root, "missing.txt")) + "\n");
Io.print("executable-missing:" + Fs.isExecutable(join(root, "missing.txt")) + "\n");
Io.print("issymlink-missing:" + Fs.isSymlink(join(root, "missing.txt")) + "\n");

Io.print("pathinfo-invalid:" + (Fs.pathInfo(42) == undefined) + "\n");
