var f = Io.open("sample.jpg", "rb");
//var f = Io.open("rgba.png", "rb");
var buf = f.read();
f.close();


var format = Image.detectFormat(buf);
var img;
if (format == 'png') img = Image.decodePNG(buf);
else if (format == 'jpeg') img = Image.decodeJPEG(buf);
else {
    Io.stderr.write("Unsupported image format\n");
    ProtoScript.exit(0);
}

Display.open(img.width, img.height, "ProtoScript Image");
Display.blitRGBA(img.data, img.width, img.height, 0, 0);

Display.present();

while (true) {
    var ev = Event.next();
    if (ev && ev.type == "quit") {
        break;
    }
}

Display.close();
