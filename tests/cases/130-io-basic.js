var path = Io.tempPath();
var f = Io.open(path, "w");
f.write("alpha\nbeta");
f.close();

f = Io.open(path, "r");
Io.print((f.read()) + "\n");
f.close();

var path2 = Io.tempPath();
var f2 = Io.open(path2, "w");
f2.write("x\ny");
f2.close();

f2 = Io.open(path2, "r");
var data = f2.read();
f2.close();
var lines = data.split("\n");
Io.print((lines.length) + "\n");
Io.print((lines[0]) + "\n");
Io.print((lines[1]) + "\n");
