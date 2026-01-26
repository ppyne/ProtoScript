include "cpu_filters.js";

CG.DEBUG_QUANT = true;
RgbQuant.DEBUG = true;
RgbQuant.DEBUG_FILTER = ["buildPal", "reduce image", "ditherImage", "perf"];

Io.stderr.write("indexed_colors_examples: decode JPEG\n");
var f = Io.open("sample.jpg", "rb");
var buf = f.read();
f.close();

var img = Image.decodeJPEG(buf);

Io.stderr.write("indexed_colors_examples: indexedColor start\n");
var out = CG.indexedColor(img, 16, "ada", "FloydSteinberg", false);
Io.stderr.write("indexed_colors_examples: indexedColor done\n");

var png = Image.encodePNG(out);
var outFile = Io.open("sample_indexed_colors_example.png", "wb");
outFile.write(png);
outFile.close();

var s = ProtoScript.perfStats();
Io.stderr.write("perfStats bufferReadIndexFast=" + s.bufferReadIndexFast +
    " bufferWriteIndexFast=" + s.bufferWriteIndexFast +
    " bufferReadIndex=" + s.bufferReadIndex +
    " bufferWriteIndex=" + s.bufferWriteIndex +
    " arrayGet=" + s.arrayGet +
    " arraySet=" + s.arraySet +
    " objectGet=" + s.objectGet +
    " objectPut=" + s.objectPut +
    " callCount=" + s.callCount +
    " evalNodeCount=" + s.evalNodeCount +
    " evalExprCount=" + s.evalExprCount + "\n");
