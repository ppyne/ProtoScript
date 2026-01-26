/*
* Copyright (c) 2015, Leon Sorokin
* All rights reserved. (MIT Licensed)
*
* RgbQuant.js - an image quantization lib
* Ported to ProtoScript RGBA8 images.
*/

function RgbQuant(opts) {
    opts = opts || {};

    // 1 = by global population, 2 = subregion population threshold
    this.method = opts.method || 2;
    // desired final palette size
    this.colors = opts.colors || 256;
    // # of highest-frequency colors to start with for palette reduction
    this.initColors = opts.initColors || 4096;
    // color-distance threshold for initial reduction pass
    this.initDist = opts.initDist || 0.01;
    // subsequent passes threshold
    this.distIncr = opts.distIncr || 0.005;
    // palette grouping
    this.hueGroups = opts.hueGroups || 10;
    this.satGroups = opts.satGroups || 10;
    this.lumGroups = opts.lumGroups || 10;
    // if > 0, enables hues stats and min-color retention per group
    this.minHueCols = opts.minHueCols || 0;
    // HueStats instance
    this.hueStats = this.minHueCols ? new HueStats(this.hueGroups, this.minHueCols) : null;

    // subregion partitioning box size
    this.boxSize = opts.boxSize || [64, 64];
    // number of same pixels required within box for histogram inclusion
    this.boxPxls = opts.boxPxls || 2;
    // palette locked indicator
    this.palLocked = false;

    // dithering/error diffusion kernel name
    this.dithKern = opts.dithKern || null;
    // dither serpentine pattern
    this.dithSerp = opts.dithSerp || false;
    // minimum color difference (0-1) needed to dither
    this.dithDelta = opts.dithDelta || 0;

    // accumulated histogram
    this.histogram = {};
    // palette - rgb triplets
    this.idxrgb = [];
    // reverse lookup {key:idx}
    this.keyidx = {};
    if (opts.palette) {
        for (var pi = 0; pi < opts.palette.length; pi++) {
            this.idxrgb[pi] = opts.palette[pi];
        }
    }
    // cache of resolved colors {key:idx}
    // max number of color-mappings to cache
    this.cacheLimit = opts.cacheLimit || 2e5;
    // allows pre-defined palettes to be re-indexed (enabling palette compacting and sorting)
    this.reIndex = opts.reIndex || this.idxrgb.length === 0;
    // sample quantization bits per channel (1-8). Lower is faster.
    this.sampleBits = opts.sampleBits || 8;
    if (this.sampleBits < 1) this.sampleBits = 1;
    if (this.sampleBits > 8) this.sampleBits = 8;

    // if pre-defined palette, build lookups
    if (this.idxrgb.length > 0) {
        for (var i = 0; i < this.idxrgb.length; i++) {
            var rgb = this.idxrgb[i];
            var key = rgbKey(rgb[0], rgb[1], rgb[2]);
            this.keyidx[key] = i;
        }
    }
    rq_debug("RgbQuant init colors=" + this.colors + " method=" + this.method +
        " box=" + this.boxSize[0] + "x" + this.boxSize[1] +
        " boxPxls=" + this.boxPxls + " dith=" + (this.dithKern || "") +
        " serp=" + (this.dithSerp ? "1" : "0"));
}

// gathers histogram info
RgbQuant.prototype.sample = function sample(img) {
    if (this.palLocked)
        throw "Cannot sample additional images, palette already assembled.";

    var data = getImageData(img);
    rq_debug("sample " + data.width + "x" + data.height + " method=" + this.method);

    switch (this.method) {
        case 1: this.colorStats1D(data.buf8); break;
        case 2: this.colorStats2D(data.buf8, data.width, data.height); break;
    }

    rq_debug("sample done");
};

// image quantizer
// retType: "image" (default), "index"
RgbQuant.prototype.reduce = function reduce(img, retType, dithKern, dithSerp) {
    if (!this.palLocked)
        this.buildPal();

    dithKern = dithKern == "None" ? "" : (dithKern || this.dithKern);
    dithSerp = typeof dithSerp != "undefined" ? dithSerp : this.dithSerp;

    retType = retType || "image";

    if (retType == "image") {
        rq_debug("reduce image " + img.width + "x" + img.height + " dith=" + (dithKern || ""));
        if (dithKern && (dithKern in ORDERED_DITHER_THRESHOLDS)) {
            var outImg = this.orderedDitherImage(img, dithKern);
            rq_debug("reduce image done (orderedDither)");
            return outImg;
        }
        if (dithKern) {
            var outImg = this.ditherImage(img, dithKern, dithSerp);
            rq_debug("reduce image done (ditherImage)");
            return outImg;
        }
        var outImg = this.reduceImage(img);
        rq_debug("reduce image done");
        return outImg;
    }

    if (retType == "index") {
        return this.reduceToIndex(img, dithKern, dithSerp);
    }

    return this.reduce(img, "image", dithKern, dithSerp);
};

RgbQuant.prototype.reduceToIndex = function reduceToIndex(img, dithKern, dithSerp) {
    var outImg;
    if (dithKern && (dithKern in ORDERED_DITHER_THRESHOLDS))
        outImg = this.orderedDitherImage(img, dithKern);
    else if (dithKern)
        outImg = this.ditherImage(img, dithKern, dithSerp);
    else
        outImg = this.reduceImage(img);

    var src = outImg.data;
    var len = Buffer.size(src);
    var outIdx = new Array(len / 4);
    var p = 0;
    for (var i = 0; i < len; i += 4) {
        var a = src[i + 3];
        if (a === 0) {
            outIdx[p++] = null;
            continue;
        }
        var key = rgbKey(src[i], src[i + 1], src[i + 2]);
        var idx = this.keyidx[key];
        outIdx[p++] = (idx === undefined ? null : idx);
    }
    return outIdx;
};

// Fast RGBA8 reduce.
RgbQuant.prototype.reduceImage = function reduceImage(img) {
    var width = img.width;
    var height = img.height;
    var src = img.data;
    var len = Buffer.size(src);
    var out = { width: width, height: height, data: Buffer.alloc(len) };
    var dst = out.data;
    var pal = this.idxrgb;
    if (!pal || pal.length === 0) {
        this.buildPal();
        pal = this.idxrgb;
    }
    var palLen = pal.length;
    if (palLen === 0)
        throw "Palette is empty; call sample() before dithering.";
    var palR = [];
    var palG = [];
    var palB = [];
    for (var p = 0; p < palLen; p++) {
        palR[p] = pal[p][0];
        palG[p] = pal[p][1];
        palB[p] = pal[p][2];
    }

    for (var i = 0; i < len; i += 4) {
        var a = src[i + 3];
        if (a === 0) {
            dst[i] = 0;
            dst[i + 1] = 0;
            dst[i + 2] = 0;
            dst[i + 3] = 0;
            continue;
        }
        var r = src[i];
        var g = src[i + 1];
        var b = src[i + 2];
        var best = 0;
        var bestDist = 1e30;
        for (var p = 0; p < palLen; p++) {
            var pr = pal[p][0];
            var pg = pal[p][1];
            var pb = pal[p][2];
            var dist = colorDistSqRGB(r, g, b, pr, pg, pb);
            if (dist < bestDist) {
                bestDist = dist;
                best = p;
            }
        }
        var sel = pal[best];
        dst[i] = sel[0];
        dst[i + 1] = sel[1];
        dst[i + 2] = sel[2];
        dst[i + 3] = a;
    }
    return out;
};

// Dithered reduce using RGBA8 buffers.
RgbQuant.prototype.ditherImage = function (img, kernel, serpentine) {
    var kernels = {
        FloydSteinberg: [
            [7 / 16, 1, 0],
            [3 / 16, -1, 1],
            [5 / 16, 0, 1],
            [1 / 16, 1, 1]
        ],
        FalseFloydSteinberg: [
            [3 / 8, 1, 0],
            [3 / 8, 0, 1],
            [2 / 8, 1, 1]
        ],
        Stucki: [
            [8 / 42, 1, 0],
            [4 / 42, 2, 0],
            [2 / 42, -2, 1],
            [4 / 42, -1, 1],
            [8 / 42, 0, 1],
            [4 / 42, 1, 1],
            [2 / 42, 2, 1],
            [1 / 42, -2, 2],
            [2 / 42, -1, 2],
            [4 / 42, 0, 2],
            [2 / 42, 1, 2],
            [1 / 42, 2, 2]
        ],
        Atkinson: [
            [1 / 8, 1, 0],
            [1 / 8, 2, 0],
            [1 / 8, -1, 1],
            [1 / 8, 0, 1],
            [1 / 8, 1, 1],
            [1 / 8, 0, 2]
        ],
        Jarvis: [
            [7 / 48, 1, 0],
            [5 / 48, 2, 0],
            [3 / 48, -2, 1],
            [5 / 48, -1, 1],
            [7 / 48, 0, 1],
            [5 / 48, 1, 1],
            [3 / 48, 2, 1],
            [1 / 48, -2, 2],
            [3 / 48, -1, 2],
            [5 / 48, 0, 2],
            [3 / 48, 1, 2],
            [1 / 48, 2, 2]
        ],
        Burkes: [
            [8 / 32, 1, 0],
            [4 / 32, 2, 0],
            [2 / 32, -2, 1],
            [4 / 32, -1, 1],
            [8 / 32, 0, 1],
            [4 / 32, 1, 1],
            [2 / 32, 2, 1]
        ],
        Sierra: [
            [5 / 32, 1, 0],
            [3 / 32, 2, 0],
            [2 / 32, -2, 1],
            [4 / 32, -1, 1],
            [5 / 32, 0, 1],
            [4 / 32, 1, 1],
            [2 / 32, 2, 1],
            [2 / 32, -1, 2],
            [3 / 32, 0, 2],
            [2 / 32, 1, 2]
        ],
        TwoSierra: [
            [4 / 16, 1, 0],
            [3 / 16, 2, 0],
            [1 / 16, -2, 1],
            [2 / 16, -1, 1],
            [3 / 16, 0, 1],
            [2 / 16, 1, 1],
            [1 / 16, 2, 1]
        ],
        SierraLite: [
            [2 / 4, 1, 0],
            [1 / 4, -1, 1],
            [1 / 4, 0, 1]
        ]
    };

    if (!kernel || !kernels[kernel]) {
        throw "Unknown dithering kernel: " + kernel;
    }

    var ds = kernels[kernel];
    var width = img.width;
    var height = img.height;
    var src = img.data;
    var len = Buffer.size(src);
    var work = Buffer.alloc(len);
    var out = { width: width, height: height, data: Buffer.alloc(len) };
    var dst = out.data;
    for (var i = 0; i < len; i++) {
        var v = src[i];
        work[i] = v;
        dst[i] = v;
    }
    var pal = this.idxrgb;
    var palLen = pal.length;
    var palR = [];
    var palG = [];
    var palB = [];
    for (var p = 0; p < palLen; p++) {
        palR[p] = pal[p][0];
        palG[p] = pal[p][1];
        palB[p] = pal[p][2];
    }
    var dir = serpentine ? -1 : 1;
    var t0 = (new Date()).getTime();
    var row0Start = 0;
    var row0Count = 0;
    rq_debug("ditherImage start " + width + "x" + height + " kernel=" + kernel +
        " serp=" + (serpentine ? "1" : "0"));

    for (var y = 0; y < height; y++) {
        if ((y % 50) === 0) {
            rq_debug("ditherImage row " + y + "/" + height +
                " elapsed=" + ((new Date()).getTime() - t0) + "ms");
        }
        if (y === 0) {
            row0Start = (new Date()).getTime();
            row0Count = 0;
        }
        if (serpentine)
            dir = dir * -1;

        var x = (dir == 1 ? 0 : width - 1);
        var xend = (dir == 1 ? width : 0);

        while (x !== xend) {
            if (y === 0) {
                row0Count++;
                if ((row0Count % 64) === 0) {
                    rq_debug("ditherImage row0 x=" + x +
                        " count=" + row0Count +
                        " elapsed=" + ((new Date()).getTime() - row0Start) + "ms");
                }
                if (x >= 388 && x < 392) {
                    rq_debug("ditherImage row0 state x=" + x +
                        " dir=" + dir + " xend=" + xend);
                }
            }
            var row0StepStart = 0;
            if (y === 0 && x >= 384 && x < 388) {
                row0StepStart = (new Date()).getTime();
            }
            var idx = (y * width + x) * 4;
            var r1 = work[idx];
            var g1 = work[idx + 1];
            var b1 = work[idx + 2];
            var a1 = work[idx + 3];

            var best = 0;
            var bestDist = 1e30;
            for (var p = 0; p < palLen; p++) {
                var pr = palR[p];
                var pg = palG[p];
                var pb = palB[p];
                var dist = colorDistRGB(r1, g1, b1, pr, pg, pb);
                if (dist < bestDist) {
                    bestDist = dist;
                    best = p;
                }
            }
            var r2 = palR[best];
            var g2 = palG[best];
            var b2 = palB[best];

            dst[idx] = r2;
            dst[idx + 1] = g2;
            dst[idx + 2] = b2;
            dst[idx + 3] = a1;

            if (this.dithDelta) {
                var distChk = colorDist([r1, g1, b1], [r2, g2, b2]);
                if (distChk < this.dithDelta) {
                    x += dir;
                    continue;
                }
            }

            var er = r1 - r2;
            var eg = g1 - g2;
            var eb = b1 - b2;

            var k = (dir == 1 ? 0 : ds.length - 1);
            var end = (dir == 1 ? ds.length : 0);
            while (k !== end) {
                var x1 = ds[k][1] * dir;
                var y1 = ds[k][2];
                var d = ds[k][0];
                var nx = x + x1;
                var ny = y + y1;
                if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                    var nidx = (ny * width + nx) * 4;
                    work[nidx] = clamp8(work[nidx] + er * d);
                    work[nidx + 1] = clamp8(work[nidx + 1] + eg * d);
                    work[nidx + 2] = clamp8(work[nidx + 2] + eb * d);
                }
                k += dir;
            }

            if (row0StepStart) {
                rq_debug("ditherImage row0 step x=" + x +
                    " stepMs=" + ((new Date()).getTime() - row0StepStart));
            }
            x += dir;
        }
    }

    rq_debug("ditherImage done elapsed=" + ((new Date()).getTime() - t0) + "ms");
    return out;
};

// adapted from http://jsbin.com/iXofIji/2/edit by PAEz
RgbQuant.prototype.dither = function (img, kernel, serpentine) {
    return this.ditherImage(img, kernel, serpentine);
};

// adapted from http://bisqwit.iki.fi/story/howto/dither/jy/
RgbQuant.prototype.orderedDitherImage = function (img, kernel) {
    var map = ORDERED_DITHER_THRESHOLDS[kernel];
    if (!map) {
        throw "Unknown dithering kernel: " + kernel;
    }

    var width = img.width;
    var height = img.height;
    var src = img.data;
    var len = Buffer.size(src);
    var out = { width: width, height: height, data: Buffer.alloc(len) };
    var dst = out.data;

    var mapW = map.length;
    var mapH = map[0].length;
    var brightnessOffset = mapW * mapH / 3;

    var pal = this.palette(true);
    var depth = [[], [], []];
    for (var p = 0; p < pal.length; p++) {
        var rgb = pal[p];
        depth[0][depth[0].length] = rgb[0];
        depth[1][depth[1].length] = rgb[1];
        depth[2][depth[2].length] = rgb[2];
    }
    for (var c = 0; c < 3; c++) {
        depth[c].sort(function (a, b) { return a - b; });
        var maximum = 1;
        for (var i = 1; i < depth[c].length; i++) {
            var diff = depth[c][i] - depth[c][i - 1];
            if (diff > maximum) maximum = diff;
        }
        depth[c] = maximum / (mapW * mapH);
    }

    function boundColorValue(value) {
        return Math.max(0, Math.min(255, Math.floor(value)));
    }

    var mY = 0;
    for (var y = 0; y < height; y++) {
        var lni = y * width;
        mY++;
        if (mY >= mapH) mY = 0;
        var mapRow = map[mY];

        var mX = 0;
        for (var x = 0; x !== width; x++) {
            mX++;
            if (mX >= mapW) mX = 0;
            var mapValue = mapRow[mX];

            var idx = (lni + x) * 4;
            var r1 = src[idx];
            var g1 = src[idx + 1];
            var b1 = src[idx + 2];
            var a1 = src[idx + 3];
            if (a1 === 0) {
                dst[idx] = 0;
                dst[idx + 1] = 0;
                dst[idx + 2] = 0;
                dst[idx + 3] = 0;
                continue;
            }

            var r2 = boundColorValue(r1 + (mapValue - brightnessOffset) * depth[0]);
            var g2 = boundColorValue(g1 + (mapValue - brightnessOffset) * depth[1]);
            var b2 = boundColorValue(b1 + (mapValue - brightnessOffset) * depth[2]);

            var idxPal = this.nearestIndex(r2, g2, b2);
            var sel = this.idxrgb[idxPal];
            dst[idx] = sel[0];
            dst[idx + 1] = sel[1];
            dst[idx + 2] = sel[2];
            dst[idx + 3] = a1;
        }
    }

    return out;
};

RgbQuant.prototype.orderedDither = function (img, kernel) {
    return this.orderedDitherImage(img, kernel);
};

// reduces histogram to palette, remaps & memoizes reduced colors
RgbQuant.prototype.buildPal = function buildPal(noSort) {
    rq_debug("buildPal start");
    if (this.palLocked || this.idxrgb.length > 0 && this.idxrgb.length <= this.colors) return;

    var histG = this.histogram;
    var sorted = sortedHashKeys(histG, true);

    if (sorted.length == 0)
        throw "Nothing has been sampled, palette cannot be built.";

    var idxkeys;
    switch (this.method) {
        case 1:
            var cols = this.initColors;
            var last = sorted[cols - 1];
            var freq = histG[last];

            idxkeys = [];
            for (var i = 0; i < cols && i < sorted.length; i++) {
                idxkeys[i] = sorted[i];
            }

            var pos = cols;
            var len = sorted.length;
            while (pos < len && histG[sorted[pos]] == freq) {
                idxkeys[idxkeys.length] = sorted[pos++];
            }

            if (this.hueStats)
                this.hueStats.inject(idxkeys);
            break;
        case 2:
            idxkeys = sorted;
            break;
    }

    this.reducePal(idxkeys);

    if (!noSort && this.reIndex)
        this.sortPal();

    this.palLocked = true;
    rq_debug("buildPal sorted=" + sorted.length + " palette=" + this.idxrgb.length);
};

RgbQuant.prototype.palette = function palette(tuples, noSort) {
    this.buildPal(noSort);
    if (tuples) return this.idxrgb;
    var out = [];
    for (var i = 0; i < this.idxrgb.length; i++) {
        var rgb = this.idxrgb[i];
        out[out.length] = rgb[0];
        out[out.length] = rgb[1];
        out[out.length] = rgb[2];
        out[out.length] = 255;
    }
    return out;
};

RgbQuant.prototype.prunePal = function prunePal(keep) {
    for (var j = 0; j < this.idxrgb.length; j++) {
        if (!keep[j]) {
            var rgb = this.idxrgb[j];
            if (rgb) {
                var key = rgbKey(rgb[0], rgb[1], rgb[2]);
                delete this.keyidx[key];
            }
            this.idxrgb[j] = null;
        }
    }

    if (this.reIndex) {
        var idxrgb = [];

        var i = 0;
        for (var j = 0; j < this.idxrgb.length; j++) {
            if (this.idxrgb[j]) {
                var rgb = this.idxrgb[j];
                idxrgb[i] = rgb;
                i++;
            }
        }

        this.idxrgb = idxrgb;
        this.keyidx = {};
        for (var k = 0; k < this.idxrgb.length; k++) {
            var rgb2 = this.idxrgb[k];
            this.keyidx[rgbKey(rgb2[0], rgb2[1], rgb2[2])] = k;
        }
    }
};

// reduces similar colors from an importance-sorted RGB list
RgbQuant.prototype.reducePal = function reducePal(keys) {
    rq_debug("reducePal start keys=" + keys.length + " colors=" + this.colors +
        " idxrgb=" + this.idxrgb.length);

    var idxrgb = new Array(keys.length);
    for (var i = 0; i < keys.length; i++) {
        idxrgb[i] = keyToRgb(keys[i]);
    }

    if (this.idxrgb.length > this.colors) {
        var len = idxrgb.length;
        var keep = {};
        var uniques = 0;
        var pruned = false;

        for (var i = 0; i < len; i++) {
            if ((i % 1000) === 0)
                rq_debug("reducePal scan " + i + "/" + len + " uniques=" + uniques);
            if (uniques == this.colors && !pruned) {
                this.prunePal(keep);
                pruned = true;
            }

            var rgb = idxrgb[i];
            var idx = this.nearestIndex(rgb[0], rgb[1], rgb[2]);

            if (uniques < this.colors && !keep[idx]) {
                keep[idx] = true;
                uniques++;
            }
        }

        if (!pruned) {
            this.prunePal(keep);
            pruned = true;
        }
    }
    else {
        var len = idxrgb.length;
        var palLen = len;
        var thold = this.initDist;
        rq_debug("reducePal build idxrgb len=" + len + " palLen=" + palLen);

        if (palLen > this.colors) {
            while (palLen > this.colors) {
                var memDist = [];
                rq_debug("reducePal dist pass palLen=" + palLen + " thold=" + thold);

                for (var i = 0; i < len; i++) {
                    var pxi = idxrgb[i];
                    if (!pxi) continue;

                    for (var j = i + 1; j < len; j++) {
                        var pxj = idxrgb[j];
                        if (!pxj) continue;

                        var dist = colorDist(pxi, pxj);

                        if (dist < thold) {
                            memDist[memDist.length] = [j, pxj, dist];
                            delete (idxrgb[j]);
                            palLen--;
                        }
                    }
                }

                thold += (palLen > this.colors * 3) ? this.initDist : this.distIncr;
            }

            if (palLen < this.colors) {
                memDist.sort(function (a, b) {
                    return b[2] - a[2];
                });

                var k = 0;
                while (palLen < this.colors) {
                    idxrgb[memDist[k][0]] = memDist[k][1];
                    palLen++;
                    k++;
                }
            }
        }

        var merged = [];
        for (var i = 0; i < idxrgb.length; i++) {
            if (!idxrgb[i]) continue;
            merged[merged.length] = idxrgb[i];
        }
        this.idxrgb = merged;
        this.keyidx = {};
        for (var i = 0; i < this.idxrgb.length; i++) {
            var rgb2 = this.idxrgb[i];
            this.keyidx[rgbKey(rgb2[0], rgb2[1], rgb2[2])] = i;
        }
    }
    rq_debug("reducePal done idxrgb=" + this.idxrgb.length);
};

// global top-population
RgbQuant.prototype.colorStats1D = function colorStats1D(buf8) {
    var histG = this.histogram;
    var len = Buffer.size(buf8);
    var shift = 8 - this.sampleBits;
    var col;

    for (var i = 0; i < len; i += 4) {
        var a = buf8[i + 3];
        if (a === 0) continue;
        var r = buf8[i];
        var g = buf8[i + 1];
        var b = buf8[i + 2];
        if (shift > 0) {
            r = (r >> shift) << shift;
            g = (g >> shift) << shift;
            b = (b >> shift) << shift;
        }
        col = rgbKey(r, g, b);

        if (this.hueStats)
            this.hueStats.check(r, g, b);

        var v = histG[col];
        if (v === undefined)
            histG[col] = 1;
        else
            histG[col] = v + 1;
    }
};

// population threshold within subregions
RgbQuant.prototype.colorStats2D = function colorStats2D(buf8, width, height) {
    var boxW = this.boxSize[0];
    var boxH = this.boxSize[1];
    var area = boxW * boxH;
    var boxes = makeBoxes(width, height, boxW, boxH);
    var histG = this.histogram;
    var self = this;
    var shift = 8 - this.sampleBits;
    var t0 = (new Date()).getTime();
    var pixTotal = 0;
    rq_debug("colorStats2D start boxes=" + boxes.length + " box=" + boxW + "x" + boxH);

    for (var bi = 0; bi < boxes.length; bi++) {
        rq_debug("colorStats2D box " + bi + "/" + boxes.length +
            " elapsed=" + ((new Date()).getTime() - t0) + "ms");
        var box = boxes[bi];
        var effc = Math.max(Math.round((box.w * box.h) / area) * self.boxPxls, 2);
        var histL = {};
        var col;
        var boxPix = 0;
        var tBox = (new Date()).getTime();

        for (var y = box.y; y < box.y + box.h; y++) {
            var row = y * width;
            for (var x = box.x; x < box.x + box.w; x++) {
                var i = (row + x) * 4;
                boxPix++;
                pixTotal++;
                var a = buf8[i + 3];
                if (a === 0) continue;
                var r = buf8[i];
                var g = buf8[i + 1];
                var b = buf8[i + 2];
                if (shift > 0) {
                    r = (r >> shift) << shift;
                    g = (g >> shift) << shift;
                    b = (b >> shift) << shift;
                }
                col = rgbKey(r, g, b);

                if (self.hueStats)
                    self.hueStats.check(r, g, b);

                var gv = histG[col];
                if (gv !== undefined)
                    histG[col] = gv + 1;
                else {
                    var lv = histL[col];
                    if (lv !== undefined) {
                        lv = lv + 1;
                        histL[col] = lv;
                        if (lv >= effc)
                            histG[col] = lv;
                    }
                    else
                        histL[col] = 1;
                }
            }
        }
        rq_debug("colorStats2D box " + bi + " pixels=" + boxPix +
            " boxMs=" + ((new Date()).getTime() - tBox) + " totalPix=" + pixTotal);
    }

    if (this.hueStats)
        this.hueStats.inject(histG);
    rq_debug("colorStats2D done totalPix=" + pixTotal);
};

RgbQuant.prototype.sortPal = function sortPal() {
    rq_debug("sortPal start idxrgb=" + this.idxrgb.length);
    var self = this;

    this.idxrgb.sort(function (rgbA, rgbB) {

        var hslA = rgb2hsl(rgbA[0], rgbA[1], rgbA[2]);
        var hslB = rgb2hsl(rgbB[0], rgbB[1], rgbB[2]);

        var hueA = (rgbA[0] == rgbA[1] && rgbA[1] == rgbA[2]) ? -1 : hueGroup(hslA.h, self.hueGroups);
        var hueB = (rgbB[0] == rgbB[1] && rgbB[1] == rgbB[2]) ? -1 : hueGroup(hslB.h, self.hueGroups);

        var hueDiff = hueB - hueA;
        if (hueDiff) return -hueDiff;

        var lumDiff = lumGroup(+hslB.l.toFixed(2)) - lumGroup(+hslA.l.toFixed(2));
        if (lumDiff) return -lumDiff;

        var satDiff = satGroup(+hslB.s.toFixed(2)) - satGroup(+hslA.s.toFixed(2));
        if (satDiff) return -satDiff;
    });

    this.keyidx = {};
    for (var i = 0; i < this.idxrgb.length; i++) {
        var rgb = this.idxrgb[i];
        self.keyidx[rgbKey(rgb[0], rgb[1], rgb[2])] = i;
    }
    rq_debug("sortPal done idxrgb=" + this.idxrgb.length);
};

RgbQuant.prototype.nearestColor = function nearestColor(r, g, b) {
    var idx = this.nearestIndex(r, g, b);
    if (idx === null || idx === undefined) return null;
    return this.idxrgb[idx];
};

RgbQuant.prototype.nearestIndex = function nearestIndex(r0, g0, b0) {
    var key = rgbKey(r0, g0, b0);
    if (this.keyidx[key] || this.keyidx[key] === 0)
        return this.keyidx[key];

    var min = 1e30;
    var idx;
    var len = this.idxrgb.length;

    for (var i = 0; i < len; i++) {
        if (!this.idxrgb[i]) continue;

        var rgb = this.idxrgb[i];
        var dist = colorDistSqRGB(r0, g0, b0, rgb[0], rgb[1], rgb[2]);

        if (dist < min) {
            min = dist;
            idx = i;
        }
    }

    this.cacheColor(key, idx);

    return idx;
};

var numCached = 0;

RgbQuant.prototype.cacheColor = function cacheColor(key, idx) {
    if (numCached == this.cacheLimit || this.keyidx[key] !== undefined) return;
    this.keyidx[key] = idx;
    numCached++;
};

RgbQuant.prototype.toRgbImage = function (img) {
    return getImageData(img);
};

function HueStats(numGroups, minCols) {
    this.numGroups = numGroups;
    this.minCols = minCols;
    this.stats = {};

    for (var i = -1; i < numGroups; i++)
        this.stats[i] = { num: 0, cols: [] };

    this.groupsFull = 0;
}

HueStats.prototype.check = function checkHue(r, g, b) {
    if (this.groupsFull == this.numGroups + 1)
        this.check = function () { return; };

    var hg = (r == g && g == b) ? -1 : hueGroup(rgb2hsl(r, g, b).h, this.numGroups);
    var gr = this.stats[hg];
    var min = this.minCols;

    gr.num++;

    if (gr.num > min)
        return;
    if (gr.num == min)
        this.groupsFull++;

    if (gr.num <= min)
        this.stats[hg].cols[this.stats[hg].cols.length] = rgbKey(r, g, b);
};

HueStats.prototype.inject = function injectHues(histG) {
    for (var i = -1; i < this.numGroups; i++) {
        if (this.stats[i].num <= this.minCols) {
            if (histG && typeof histG.length == "number") {
                for (var c = 0; c < this.stats[i].cols.length; c++) {
                    var key = this.stats[i].cols[c];
                    if (arr_index_of(histG, key) == -1)
                        histG[histG.length] = key;
                }
            } else {
                for (var c = 0; c < this.stats[i].cols.length; c++) {
                    var key = this.stats[i].cols[c];
                    if (!histG[key])
                        histG[key] = 1;
                    else
                        histG[key]++;
                }
            }
        }
    }
};

var Pr = .2126;
var Pg = .7152;
var Pb = .0722;

function rgb2lum(r, g, b) {
    return Math.sqrt(
        Pr * r * r +
        Pg * g * g +
        Pb * b * b
    );
}

var rd = 255;
var gd = 255;
var bd = 255;
var maxDist = Math.sqrt(Pr * rd * rd + Pg * gd * gd + Pb * bd * bd);

function colorDist(rgb0, rgb1) {
    var rd = rgb1[0] - rgb0[0];
    var gd = rgb1[1] - rgb0[1];
    var bd = rgb1[2] - rgb0[2];

    return Math.sqrt(Pr * rd * rd + Pg * gd * gd + Pb * bd * bd) / maxDist;
}

function colorDistSqRGB(r0, g0, b0, r1, g1, b1) {
    var rd = r1 - r0;
    var gd = g1 - g0;
    var bd = b1 - b0;
    return Pr * rd * rd + Pg * gd * gd + Pb * bd * bd;
}

function colorDistRGB(r0, g0, b0, r1, g1, b1) {
    var rd = r1 - r0;
    var gd = g1 - g0;
    var bd = b1 - b0;
    return Math.sqrt(Pr * rd * rd + Pg * gd * gd + Pb * bd * bd) / maxDist;
}

function rgb2hsl(r, g, b) {
    var max;
    var min;
    var h;
    var s;
    var l;
    var d;
    r /= 255;
    g /= 255;
    b /= 255;
    max = Math.max(r, g, b);
    min = Math.min(r, g, b);
    l = (max + min) / 2;
    if (max == min) {
        h = s = 0;
    } else {
        d = max - min;
        s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
        switch (max) {
            case r: h = (g - b) / d + (g < b ? 6 : 0); break;
            case g: h = (b - r) / d + 2; break;
            case b: h = (r - g) / d + 4; break;
        }
        h /= 6;
    }
    return {
        h: h,
        s: s,
        l: rgb2lum(r, g, b)
    };
}

function hueGroup(hue, segs) {
    var seg = 1 / segs;
    var haf = seg / 2;

    if (hue >= 1 - haf || hue <= haf)
        return 0;

    for (var i = 1; i < segs; i++) {
        var mid = i * seg;
        if (hue >= mid - haf && hue <= mid + haf)
            return i;
    }
}

function satGroup(sat) {
    return sat;
}

function lumGroup(lum) {
    return lum;
}

// image object -> {buf8,width,height}
function getImageData(img) {
    return {
        buf8: img.data,
        width: img.width,
        height: img.height
    };
}

function rgbKey(r, g, b) {
    return r + g * 256 + b * 65536;
}

function keyToRgb(key) {
    var v = key - 0;
    var b = Math.floor(v / 65536);
    var rem = v - b * 65536;
    var g = Math.floor(rem / 256);
    var r = rem - g * 256;
    return [r, g, b];
}

function makeBoxes(wid, hgt, w0, h0) {
    var wnum = ~~(wid / w0);
    var wrem = wid % w0;
    var hnum = ~~(hgt / h0);
    var hrem = hgt % h0;
    var xend = wid - wrem;
    var yend = hgt - hrem;

    var bxs = [];
    for (var y = 0; y < hgt; y += h0)
        for (var x = 0; x < wid; x += w0)
            bxs[bxs.length] = { x: x, y: y, w: (x == xend ? wrem : w0), h: (y == yend ? hrem : h0) };

    return bxs;
}

function iterBox(bbox, wid, fn) {
    var b = bbox;
    var i0 = b.y * wid + b.x;
    var i1 = (b.y + b.h - 1) * wid + (b.x + b.w - 1);
    var cnt = 0;
    var incr = wid - b.w + 1;
    var i = i0;

    do {
        fn.call(this, i);
        i += (++cnt % b.w == 0) ? incr : 1;
    } while (i <= i1);
}

function sortedHashKeys(obj, desc) {
    var keys = [];
    var t0 = (new Date()).getTime();

    for (var key in obj) {
        keys[keys.length] = key;
        if ((keys.length % 10000) === 0) {
            rq_debug("sortedHashKeys " + keys.length + " elapsed=" +
                ((new Date()).getTime() - t0) + "ms");
        }
    }

    rq_debug("sortedHashKeys done count=" + keys.length +
        " elapsed=" + ((new Date()).getTime() - t0) + "ms");
    sortKeysByValue(keys, obj, desc);
    rq_debug("sortedHashKeys sorted count=" + keys.length +
        " elapsed=" + ((new Date()).getTime() - t0) + "ms");
    return keys;
}

function sortKeysByValue(keys, obj, desc) {
    function cmp(a, b) {
        var diff = desc ? obj[b] - obj[a] : obj[a] - obj[b];
        if (diff) return diff;
        return a - b;
    }

    function quickSort(left, right) {
        var i = left;
        var j = right;
        var pivot = keys[(left + right) >> 1];
        while (i <= j) {
            while (cmp(keys[i], pivot) < 0) i++;
            while (cmp(keys[j], pivot) > 0) j--;
            if (i <= j) {
                var tmp = keys[i];
                keys[i] = keys[j];
                keys[j] = tmp;
                i++;
                j--;
            }
        }
        if (left < j) quickSort(left, j);
        if (i < right) quickSort(i, right);
    }

    if (keys.length > 1)
        quickSort(0, keys.length - 1);
}

function arr_index_of(arr, value) {
    for (var i = 0; i < arr.length; i++) {
        if (arr[i] === value) return i;
    }
    return -1;
}

function clamp8(value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return Math.floor(value);
}

function rq_debug(msg) {
    if (!RgbQuant.DEBUG) return;
    if (RgbQuant.DEBUG_FILTER && RgbQuant.DEBUG_FILTER.length) {
        var ok = false;
        for (var i = 0; i < RgbQuant.DEBUG_FILTER.length; i++) {
            if (msg.indexOf(RgbQuant.DEBUG_FILTER[i]) === 0) {
                ok = true;
                break;
            }
        }
        if (!ok) return;
    }
    if (typeof Io !== "undefined" && Io.stderr && Io.stderr.write) {
        Io.stderr.write("[RgbQuant] " + msg + "\n");
    }
}

RgbQuant.DEBUG = false;
RgbQuant.DEBUG_FILTER = null;

var ORDERED_DITHER_THRESHOLDS = {
    Ordered2: [
        [1, 3],
        [4, 2]
    ],
    Ordered3: [
        [3, 7, 4],
        [6, 1, 9],
        [2, 8, 5]
    ],
    Ordered4: [
        [1, 9, 3, 11],
        [13, 5, 15, 7],
        [4, 12, 2, 10],
        [16, 8, 14, 6]
    ],
    Ordered8: [
        [1, 49, 13, 61, 4, 52, 16, 64],
        [33, 17, 45, 29, 36, 20, 48, 32],
        [9, 57, 5, 53, 12, 60, 8, 56],
        [41, 25, 37, 21, 44, 28, 40, 24],
        [3, 51, 15, 63, 2, 50, 14, 62],
        [35, 19, 47, 31, 34, 18, 46, 30],
        [11, 59, 7, 55, 10, 58, 6, 54],
        [43, 27, 39, 23, 42, 26, 38, 22]
    ]
};
