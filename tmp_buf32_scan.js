include "examples/computer_graphics/rgbquant.js";
var f = Io.open("sample.jpg", "rb");
var buf = f.read();
f.close();
var img = Image.decodeJPEG(buf);
var data = getImageData(img);
var buf32 = data.buf32;
var len = buf32.length;
var countColor = 0;
var countRedOnly = 0;
for (var i = 0; i < len; i += 1000) {
    var v = buf32[i];
    var r = v & 0xff;
    var g = (v >> 8) & 0xff;
    var b = (v >> 16) & 0xff;
    var a = (v >>> 24) & 0xff;
    if (a === 0) continue;
    if (g === 0 && b === 0) countRedOnly++;
    else countColor++;
}
Io.print("sampled color=" + countColor + " redOnly=" + countRedOnly + "\n");
