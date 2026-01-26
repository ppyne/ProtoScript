include "examples/computer_graphics/rgbquant.js";

var f = Io.open("sample.jpg", "rb");
var buf = f.read();
f.close();
var img = Image.decodeJPEG(buf);

var opts = { colors: 16, method: 2, boxSize: [40,40], boxPxls: 3, dithKern: "FloydSteinberg", dithSerp: true };
var rq = new RgbQuant(opts);
var data = getImageData(img);
// emulate sample
rq.colorStats2D(data.buf32, data.width, data.height);
// build palette
rq.buildPal();
Io.print("palette len=" + rq.idxrgb.length + "\n");
for (var i = 0; i < rq.idxrgb.length && i < 16; i++) {
    var rgb = rq.idxrgb[i];
    Io.print(i + ": " + rgb[0] + "," + rgb[1] + "," + rgb[2] + "\n");
}
