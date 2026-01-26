include "examples/computer_graphics/rgbquant.js";
var f = Io.open("sample.jpg", "rb");
var buf = f.read();
f.close();
var img = Image.decodeJPEG(buf);
var data = getImageData(img);
var b32 = data.buf32;
var isBuf32 = 0;
if (typeof Buffer32 !== "undefined" && Buffer32.size) {
    try {
        var s = Buffer32.size(b32);
        isBuf32 = (s > 0) ? 1 : 0;
    } catch (e) {
        isBuf32 = 0;
    }
}
Io.print("Buffer32.size? " + isBuf32 + "\n");
Io.print("Buffer.size(buf32)=" + Buffer.size(b32) + "\n");
