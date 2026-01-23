var f = Io.open("rgba.png", "rb");
var buf = f.read();
f.close();

var img = Image.decodePNG(buf);

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
