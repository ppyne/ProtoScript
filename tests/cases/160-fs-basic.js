var f = Io.open("tests/cases/fs_root.txt", "r");
var root = f.read();
f.close();

function join(a, b) {
    return a + "/" + b;
}

function has(arr, name) {
    for (var i = 0; i < arr.length; i++) {
        if (arr[i] == name) return true;
    }
    return false;
}

Io.print("exists-file:" + Fs.exists(join(root, "file.txt")) + "\n");
Io.print("exists-missing:" + Fs.exists(join(root, "missing.txt")) + "\n");
Io.print("exists-broken:" + Fs.exists(join(root, "broken_symlink")) + "\n");

Io.print("isdir-empty:" + Fs.isDir(join(root, "empty_dir")) + "\n");
Io.print("isdir-file:" + Fs.isDir(join(root, "file.txt")) + "\n");
Io.print("isdir-missing:" + Fs.isDir(join(root, "missing.txt")) + "\n");

Io.print("isfile-file:" + Fs.isFile(join(root, "file.txt")) + "\n");
Io.print("isfile-dir:" + Fs.isFile(join(root, "empty_dir")) + "\n");
Io.print("isfile-broken:" + Fs.isFile(join(root, "broken_symlink")) + "\n");
Io.print("issymlink-file:" + Fs.isSymlink(join(root, "file.txt")) + "\n");
Io.print("issymlink-linkfile:" + Fs.isSymlink(join(root, "symlink_file")) + "\n");
Io.print("issymlink-linkdir:" + Fs.isSymlink(join(root, "symlink_dir")) + "\n");
Io.print("issymlink-broken:" + Fs.isSymlink(join(root, "broken_symlink")) + "\n");

Io.print("size-file:" + Fs.size(join(root, "file.txt")) + "\n");
Io.print("size-dir:" + Fs.size(join(root, "empty_dir")) + "\n");
Io.print("size-missing:" + Fs.size(join(root, "missing.txt")) + "\n");

Io.print("readable-file:" + Fs.isReadable(join(root, "file.txt")) + "\n");
Io.print("readable-unreadable:" + Fs.isReadable(join(root, "unreadable.txt")) + "\n");
Io.print("writable-file:" + Fs.isWritable(join(root, "file.txt")) + "\n");
Io.print("executable-exec:" + Fs.isExecutable(join(root, "exec.sh")) + "\n");

var list = Fs.ls(root);
Io.print("ls-count:" + list.length + "\n");
Io.print("ls-has-file:" + has(list, "file.txt") + "\n");
Io.print("ls-has-dot:" + has(list, ".") + "\n");

var info = Fs.pathInfo("/a/b.txt");
Io.print("pathinfo-dir:" + info.dirname + "\n");
Io.print("pathinfo-base:" + info.basename + "\n");
Io.print("pathinfo-file:" + info.filename + "\n");
Io.print("pathinfo-ext:" + info.extension + "\n");

Io.print("pwd-nonempty:" + (Fs.pwd().length > 0) + "\n");

var work = join(root, "work");
Io.print("mkdir-work:" + Fs.mkdir(work) + "\n");

var a = join(work, "a.txt");
var fa = Io.open(a, "w");
fa.write("data");
fa.close();

Io.print("chmod-a:" + Fs.chmod(a, 0644) + "\n");

var b = join(work, "b.txt");
Io.print("cp-a-b:" + Fs.cp(a, b) + "\n");
Io.print("size-b:" + Fs.size(b) + "\n");

Io.print("mv-b:" + Fs.mv(b, "moved.txt") + "\n");
var moved = join(work, "moved.txt");
Io.print("exists-moved:" + Fs.exists(moved) + "\n");

Io.print("rm-moved:" + Fs.rm(moved) + "\n");
Io.print("rm-a:" + Fs.rm(a) + "\n");
Io.print("rmdir-work:" + Fs.rmdir(work) + "\n");
