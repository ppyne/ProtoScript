function loadPng(path) {
    var f = Io.open(path, "rb");
    if (!f) {
        Io.stderr.write("failed to open " + path + "\n");
        ProtoScript.exit(1);
    }
    var buf = f.read();
    f.close();
    return Image.decodePNG(buf);
}

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

function diff(imgA, imgB) {
    if (imgA.width !== imgB.width || imgA.height !== imgB.height) {
        return { error: "size mismatch" };
    }
    var a = imgA.data;
    var b = imgB.data;
    var len = Buffer.size(a);
    var diffPixels = 0;
    var diffSum = 0;
    var maxDelta = 0;
    var first = -1;
    for (var i = 0; i < len; i += 4) {
        var dr = a[i] - b[i];
        var dg = a[i + 1] - b[i + 1];
        var db = a[i + 2] - b[i + 2];
        var da = a[i + 3] - b[i + 3];
        var delta = Math.abs(dr) + Math.abs(dg) + Math.abs(db) + Math.abs(da);
        if (delta !== 0) {
            diffPixels++;
            diffSum += delta;
            if (delta > maxDelta) maxDelta = delta;
            if (first < 0) first = i;
        }
    }
    return { diffPixels: diffPixels, diffSum: diffSum, maxDelta: maxDelta, first: first };
}

var aPath = "sample_indexed_colors_example.png";
var bPath = "sample_indexed_colors_example_expected.png";
var imgA = loadPng(aPath);
var imgB = loadPng(bPath);
var sA = stats(imgA);
var sB = stats(imgB);
Io.print("A " + aPath + " " + sA.w + "x" + sA.h + " uniq=" + sA.uniq +
    " black=" + sA.black + " transparent=" + sA.transparent +
    " min=" + sA.minR + "," + sA.minG + "," + sA.minB + "," + sA.minA +
    " max=" + sA.maxR + "," + sA.maxG + "," + sA.maxB + "," + sA.maxA + "\n");
Io.print("B " + bPath + " " + sB.w + "x" + sB.h + " uniq=" + sB.uniq +
    " black=" + sB.black + " transparent=" + sB.transparent +
    " min=" + sB.minR + "," + sB.minG + "," + sB.minB + "," + sB.minA +
    " max=" + sB.maxR + "," + sB.maxG + "," + sB.maxB + "," + sB.maxA + "\n");
var d = diff(imgA, imgB);
Io.print("diff pixels=" + d.diffPixels + " diffSum=" + d.diffSum +
    " maxDelta=" + d.maxDelta + " firstByteIndex=" + d.first + "\n");
