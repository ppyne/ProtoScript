var path = Io.tempPath();
var bom = Buffer.alloc(4);
bom[0] = 0xEF;
bom[1] = 0xBB;
bom[2] = 0xBF;
bom[3] = 65; // 'A'
var out = Io.open(path, "wb");
out.write(bom);
out.close();
var inp = Io.open(path, "r");
Io.print(inp.read() + "\n");
inp.close();

var path2 = Io.tempPath();
var bad = Buffer.alloc(5);
bad[0] = 65; // 'A'
bad[1] = 0xEF;
bad[2] = 0xBB;
bad[3] = 0xBF;
bad[4] = 66; // 'B'
out = Io.open(path2, "wb");
out.write(bad);
out.close();
try {
    inp = Io.open(path2, "r");
    inp.read();
    inp.close();
} catch (e) {
    Io.print(e.name + "\n");
}

var path3 = Io.tempPath();
var bad2 = Buffer.alloc(2);
bad2[0] = 0xFF;
bad2[1] = 0xFE;
out = Io.open(path3, "wb");
out.write(bad2);
out.close();
try {
    inp = Io.open(path3, "r");
    inp.read();
    inp.close();
} catch (e) {
    Io.print(e.name + "\n");
}

var path4 = Io.tempPath();
var nul = Buffer.alloc(1);
nul[0] = 0;
out = Io.open(path4, "wb");
out.write(nul);
out.close();
try {
    inp = Io.open(path4, "r");
    inp.read();
    inp.close();
} catch (e) {
    Io.print(e.name + "\n");
}
