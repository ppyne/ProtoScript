var path = Io.tempPath();
var f = Io.open(path, "w");
Io.write(f, "alpha\nbeta");
Io.close(f);

f = Io.open(path, "r");
Io.print((Io.read(f)) + "\n");
Io.close(f);

var path2 = Io.tempPath();
var f2 = Io.open(path2, "w");
Io.write(f2, "x\ny");
Io.close(f2);

f2 = Io.open(path2, "r");
var lines = Io.readLines(f2);
Io.close(f2);
Io.print((lines.length) + "\n");
Io.print((lines[0]) + "\n");
Io.print((lines[1]) + "\n");
