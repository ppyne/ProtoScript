var root = Io.tempPath();
Fs.mkdir(root);
var target = root + "/script.sh";
var f = Io.open(target, "w");
f.write("echo ok\n");
f.close();

Fs.chmod(target, 0755);
if (Fs.isExecutable(target)) Io.print("ready" + Io.EOL);

Fs.chmod(target, 0644);
Fs.rm(target);
Fs.rmdir(root);
