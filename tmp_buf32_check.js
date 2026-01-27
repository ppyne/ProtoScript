var f = Io.open("sample.jpg", "rb");
var buf = f.read();
f.close();
var img = Image.decodeJPEG(buf);
var b = img.data;
var r = b[0];
var g = b[1];
var bb = b[2];
var a = b[3];
Io.print("pixel0 rgba=" + r + "," + g + "," + bb + "," + a + "\n");

if (typeof Buffer32 !== "undefined" && Buffer32.view) {
    var b32 = Buffer32.view(b);
    var v = b32[0];
    var rr = v & 0xff;
    var gg = (v >> 8) & 0xff;
    var bb2 = (v >> 16) & 0xff;
    var aa = (v >>> 24) & 0xff;
    Io.print("buf32[0]=" + v + " unpack=" + rr + "," + gg + "," + bb2 + "," + aa + "\n");
}
