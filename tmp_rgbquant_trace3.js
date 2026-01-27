include "examples/computer_graphics/rgbquant.js";

var f = Io.open("sample.jpg", "rb");
var buf = f.read();
f.close();
var img = Image.decodeJPEG(buf);
Io.stderr.write("decoded\n");
var rq = new RgbQuant({ colors: 16, method: 1, sampleBits: 5, initColors: 256, initDist: 0.1, distIncr: 0.05, dithKern: "FloydSteinberg", dithSerp: true });
var sample = img;
var maxDim = 64;
if (img.width > maxDim || img.height > maxDim) {
    var scale = Math.min(maxDim / img.width, maxDim / img.height);
    sample = Image.resample(img, Math.round(img.width * scale), Math.round(img.height * scale), "linear");
}
Io.stderr.write("sampling\n");
rq.sample(sample);
Io.stderr.write("sampled\n");
rq.buildPal();
Io.stderr.write("pal built\n");
var work = sample;
Io.stderr.write("reducing\n");
var out = rq.reduce(work, "image", "FloydSteinberg", true);
Io.stderr.write("reduced\n");
