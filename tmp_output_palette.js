function loadPng(path) {
    var f = Io.open(path, "rb");
    var buf = f.read();
    f.close();
    return Image.decodePNG(buf);
}

function topColors(img, limit) {
    var data = img.data;
    var len = Buffer.size(data);
    var hist = {};
    for (var i = 0; i < len; i += 4) {
        var r = data[i];
        var g = data[i + 1];
        var b = data[i + 2];
        var a = data[i + 3];
        var key = r | (g << 8) | (b << 16) | (a << 24);
        var v = hist[key];
        if (v === undefined) hist[key] = 1;
        else hist[key] = v + 1;
    }
    var keys = [];
    for (var k in hist) keys[keys.length] = k;
    keys.sort(function (a, b) { return hist[b] - hist[a]; });
    if (limit > keys.length) limit = keys.length;
    for (var i = 0; i < limit; i++) {
        var key = +keys[i];
        var r = key & 0xff;
        var g = (key >> 8) & 0xff;
        var b = (key >> 16) & 0xff;
        var a = (key >>> 24) & 0xff;
        Io.print(i + ": " + r + "," + g + "," + b + "," + a + " count=" + hist[keys[i]] + "\n");
    }
}

var img = loadPng("sample_indexed_colors_example.png");
Io.print("unique colors...\n");
topColors(img, 16);
