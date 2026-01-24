var f = Io.open("header.png", "rb");
var buf = f.read();
f.close();

var img = Image.decodePNG(buf);
Io.print("decoded: " + img.width + "x" + img.height + "\n");

var half = Image.resample(img, img.width / 2, img.height / 2, "linear");
Io.print("resampled: " + half.width + "x" + half.height + "\n");
