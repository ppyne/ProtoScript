include "examples/computer_graphics/rgbquant.js";
var f = Io.open("sample.jpg", "rb");
var buf = f.read();
f.close();
var img = Image.decodeJPEG(buf);
var data = getImageData(img);
var hist = {};
for (var i = 0; i < 10; i++) {
    var col = data.buf32[i];
    hist[col] = i + 1;
}
var count = 0;
for (var k in hist) {
    if (count < 10) {
        Io.print("k=" + k + " val=" + hist[k] + " num=" + (+k) + "\n");
    }
    count++;
}
Io.print("count=" + count + "\n");
