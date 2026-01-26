include "examples/computer_graphics/cpu_filters.js";

Io.stderr.write("load\n");
var f = Io.open("sample.jpg", "rb");
var buf = f.read();
f.close();
var img = Image.decodeJPEG(buf);
Io.stderr.write("decoded\n");
var out = CG.indexedColor(img, 16, "ada", "FloydSteinberg", true, 64, 64);
Io.stderr.write("quantized\n");
var png = Image.encodePNG(out);
Io.stderr.write("encoded\n");
var outFile = Io.open("/tmp/sample_indexed_trace.png", "wb");
outFile.write(png);
outFile.close();
Io.stderr.write("done\n");
