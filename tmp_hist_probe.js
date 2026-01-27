include "examples/computer_graphics/rgbquant.js";
var f = Io.open("sample.jpg", "rb");
var buf = f.read();
f.close();
var img = Image.decodeJPEG(buf);
var data = getImageData(img);
var i32 = data.buf32[0];
Io.print("pixel0 i32=" + i32 + "\n");
var rq = new RgbQuant({ colors: 16, method: 2, boxSize: [40,40], boxPxls: 3 });
rq.colorStats2D(data.buf32, data.width, data.height);
var hist = rq.histogram;
var v = hist[i32];
Io.print("hist[pixel0]=" + v + "\n");
// probe with red-only key
var redKey = 4278190080; // 0xff000000
Io.print("hist[redKey]=" + hist[redKey] + "\n");
