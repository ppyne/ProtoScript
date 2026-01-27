include "examples/computer_graphics/rgbquant.js";

var f = Io.open("sample.jpg", "rb");
var buf = f.read();
f.close();
var img = Image.decodeJPEG(buf);

var rq = new RgbQuant({ colors: 16, method: 2, boxSize: [40, 40], boxPxls: 3 });
rq.sample(img);
var hist = rq.histogram;
var keys = [
    "0,0,0",
    "104,92,66",
    "150,102,0",
    "159,137,90",
    "176,19,0",
    "190,48,0",
    "191,150,0",
    "195,176,143",
    "200,231,252",
    "217,79,6",
    "225,209,158",
    "251,255,255",
    "27,39,53",
    "53,78,109",
    "64,5,0",
    "74,69,40"
];
for (var i = 0; i < keys.length; i++) {
    var key = keys[i];
    var count = hist[key];
    Io.print(key + " => " + (count === undefined ? "missing" : count) + "\n");
}
