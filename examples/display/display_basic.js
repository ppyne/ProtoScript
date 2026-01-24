Display.open(320, 240, "ProtoScript Display");
Display.clear(0xFF, 0xFF, 0xFF);
Display.line(10, 10, 310, 230, 255, 200, 0);
Display.rect(30, 30, 80, 60, 255, 0, 0);
Display.present();

while (true) {
    var ev = Event.next();
    if (ev && ev.type == "quit") {
        break;
    }
}

Display.close();
