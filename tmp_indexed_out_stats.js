include "examples/computer_graphics/cpu_filters.js";

function stats(img) {
    var data = img.data;
    var len = Buffer.size(data);
    var black = 0;
    var transparent = 0;
    var uniq = {};
    var uniqCount = 0;
    var minR = 255, minG = 255, minB = 255, minA = 255;
    var maxR = 0, maxG = 0, maxB = 0, maxA = 0;
    for (var i = 0; i < len; i += 4) {
        var r = data[i];
        var g = data[i + 1];
        var b = data[i + 2];
        var a = data[i + 3];
        if (r < minR) minR = r;
        if (g < minG) minG = g;
        if (b < minB) minB = b;
        if (a < minA) minA = a;
        if (r > maxR) maxR = r;
        if (g > maxG) maxG = g;
        if (b > maxB) maxB = b;
        if (a > maxA) maxA = a;
        if (a === 0) transparent++;
        if (r === 0 && g === 0 && b === 0 && a === 255) black++;
        var key = r | (g << 8) | (b << 16) | (a << 24);
        if (!uniq[key]) {
            uniq[key] = 1;
            uniqCount++;
        }
    }
    return {
        w: img.width,
        h: img.height,
        len: len,
        black: black,
        transparent: transparent,
        uniq: uniqCount,
        minR: minR, minG: minG, minB: minB, minA: minA,
        maxR: maxR, maxG: maxG, maxB: maxB, maxA: maxA
    };
}

var f = Io.open("sample.jpg", "rb");
var buf = f.read();
f.close();
var img = Image.decodeJPEG(buf);
var out = CG.indexedColor(img, 16, "ada", "FloydSteinberg", true);
var s = stats(out);
Io.print("out stats " + s.w + "x" + s.h + " uniq=" + s.uniq +
    " black=" + s.black + " transparent=" + s.transparent +
    " min=" + s.minR + "," + s.minG + "," + s.minB + "," + s.minA +
    " max=" + s.maxR + "," + s.maxG + "," + s.maxB + "," + s.maxA + "\n");

var png = Image.encodePNG(out);
var round = Image.decodePNG(png);
var s2 = stats(round);
Io.print("round stats " + s2.w + "x" + s2.h + " uniq=" + s2.uniq +
    " black=" + s2.black + " transparent=" + s2.transparent +
    " min=" + s2.minR + "," + s2.minG + "," + s2.minB + "," + s2.minA +
    " max=" + s2.maxR + "," + s2.maxG + "," + s2.maxB + "," + s2.maxA + "\n");
