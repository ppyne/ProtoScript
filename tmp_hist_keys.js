include "examples/computer_graphics/rgbquant.js";

var f = Io.open("sample.jpg", "rb");
var buf = f.read();
f.close();
var img = Image.decodeJPEG(buf);
var rq = new RgbQuant({ colors: 16, method: 2, boxSize: [40,40], boxPxls: 3 });
var data = getImageData(img);
rq.colorStats2D(data.buf32, data.width, data.height);
var hist = rq.histogram;
var keys = sortedHashKeys(hist, true);
Io.print("keys len=" + keys.length + "\n");
for (var i = 0; i < keys.length && i < 10; i++) {
    var k = keys[i];
    Io.print(i + " key=" + k + " val=" + hist[k] + " num=" + (+k) + "\n");
}
