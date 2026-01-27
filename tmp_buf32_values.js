include "examples/computer_graphics/rgbquant.js";
var f = Io.open("sample.jpg", "rb");
var buf = f.read();
f.close();
var img = Image.decodeJPEG(buf);
var data = getImageData(img);
Io.print("buf32 type=" + typeof data.buf32 + " len=" + data.buf32.length + "\n");
for (var i = 0; i < 5; i++) {
    var v = data.buf32[i];
    var r = v & 0xff;
    var g = (v >> 8) & 0xff;
    var b = (v >> 16) & 0xff;
    var a = (v >>> 24) & 0xff;
    Io.print(i + ": v=" + v + " rgba=" + r + "," + g + "," + b + "," + a + "\n");
}
