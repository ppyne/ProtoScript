ProtoScript.include("rgbquant.js");
var CG = {};
CG.DEBUG_QUANT = false;
CG._debugLog = function (msg) {
    if (!CG.DEBUG_QUANT) return;
    if (typeof Io !== "undefined" && Io.stderr && Io.stderr.write) {
        Io.stderr.write(msg + "\n");
    }
};

// RgbQuant palette presets and dither kernels (ElectricCat-compatible).
CG.rgbQuantDitherKernels = [
    "Atkinson",
    "Ordered2",
    "Ordered3",
    "Ordered4",
    "Ordered8",
    "Burkes",
    "FalseFloydSteinberg",
    "FloydSteinberg",
    "Jarvis",
    "",
    "Sierra",
    "TwoSierra",
    "SierraLite",
    "Stucki"
];

CG.rgbQuantPaletteData = {
    web: [
        [255, 255, 255], [255, 255, 204], [255, 255, 153], [255, 255, 102], [255, 255, 51], [255, 255, 0], [255, 204, 255],
        [255, 204, 204], [255, 204, 153], [255, 204, 102], [255, 204, 51], [255, 204, 0], [255, 153, 255], [255, 153, 204],
        [255, 153, 153], [255, 153, 102], [255, 153, 51], [255, 153, 0], [255, 102, 255], [255, 102, 204], [255, 102, 153],
        [255, 102, 102], [255, 102, 51], [255, 102, 0], [255, 51, 255], [255, 51, 204], [255, 51, 153], [255, 51, 102],
        [255, 51, 51], [255, 51, 0], [255, 0, 255], [255, 0, 204], [255, 0, 153], [255, 0, 102], [255, 0, 51], [255, 0, 0],
        [204, 255, 255], [204, 255, 204], [204, 255, 153], [204, 255, 102], [204, 255, 51], [204, 255, 0], [204, 204, 255],
        [204, 204, 204], [204, 204, 153], [204, 204, 102], [204, 204, 51], [204, 204, 0], [204, 153, 255], [204, 153, 204],
        [204, 153, 153], [204, 153, 102], [204, 153, 51], [204, 153, 0], [204, 102, 255], [204, 102, 204], [204, 102, 153],
        [204, 102, 102], [204, 102, 51], [204, 102, 0], [204, 51, 255], [204, 51, 204], [204, 51, 153], [204, 51, 102],
        [204, 51, 51], [204, 51, 0], [204, 0, 255], [204, 0, 204], [204, 0, 153], [204, 0, 102], [204, 0, 51], [204, 0, 0],
        [153, 255, 255], [153, 255, 204], [153, 255, 153], [153, 255, 102], [153, 255, 51], [153, 255, 0], [153, 204, 255],
        [153, 204, 204], [153, 204, 153], [153, 204, 102], [153, 204, 51], [153, 204, 0], [153, 153, 255], [153, 153, 204],
        [153, 153, 153], [153, 153, 102], [153, 153, 51], [153, 153, 0], [153, 102, 255], [153, 102, 204], [153, 102, 153],
        [153, 102, 102], [153, 102, 51], [153, 102, 0], [153, 51, 255], [153, 51, 204], [153, 51, 153], [153, 51, 102],
        [153, 51, 51], [153, 51, 0], [153, 0, 255], [153, 0, 204], [153, 0, 153], [153, 0, 102], [153, 0, 51], [153, 0, 0],
        [102, 255, 255], [102, 255, 204], [102, 255, 153], [102, 255, 102], [102, 255, 51], [102, 255, 0], [102, 204, 255],
        [102, 204, 204], [102, 204, 153], [102, 204, 102], [102, 204, 51], [102, 204, 0], [102, 153, 255], [102, 153, 204],
        [102, 153, 153], [102, 153, 102], [102, 153, 51], [102, 153, 0], [102, 102, 255], [102, 102, 204], [102, 102, 153],
        [102, 102, 102], [102, 102, 51], [102, 102, 0], [102, 51, 255], [102, 51, 204], [102, 51, 153], [102, 51, 102],
        [102, 51, 51], [102, 51, 0], [102, 0, 255], [102, 0, 204], [102, 0, 153], [102, 0, 102], [102, 0, 51], [102, 0, 0],
        [51, 255, 255], [51, 255, 204], [51, 255, 153], [51, 255, 102], [51, 255, 51], [51, 255, 0], [51, 204, 255], [51, 204, 204],
        [51, 204, 153], [51, 204, 102], [51, 204, 51], [51, 204, 0], [51, 153, 255], [51, 153, 204], [51, 153, 153], [51, 153, 102],
        [51, 153, 51], [51, 153, 0], [51, 102, 255], [51, 102, 204], [51, 102, 153], [51, 102, 102], [51, 102, 51], [51, 102, 0],
        [51, 51, 255], [51, 51, 204], [51, 51, 153], [51, 51, 102], [51, 51, 51], [51, 51, 0], [51, 0, 255], [51, 0, 204], [51, 0, 153],
        [51, 0, 102], [51, 0, 51], [51, 0, 0], [0, 255, 255], [0, 255, 204], [0, 255, 153], [0, 255, 102], [0, 255, 51], [0, 255, 0],
        [0, 204, 255], [0, 204, 204], [0, 204, 153], [0, 204, 102], [0, 204, 51], [0, 204, 0], [0, 153, 255], [0, 153, 204], [0, 153, 153],
        [0, 153, 102], [0, 153, 51], [0, 153, 0], [0, 102, 255], [0, 102, 204], [0, 102, 153], [0, 102, 102], [0, 102, 51], [0, 102, 0],
        [0, 51, 255], [0, 51, 204], [0, 51, 153], [0, 51, 102], [0, 51, 51], [0, 51, 0], [0, 0, 255], [0, 0, 204], [0, 0, 153], [0, 0, 102],
        [0, 0, 51], [0, 0, 0]
    ],
    mac: [[255,255,255], [255,255,204], [255,255,153], [255,255,102], [255,255,51], [255,255,0], [255,204,255], [255,204,204], [255,204,153], [255,204,102], [255,204,51], [255,204,0], [255,153,255], [255,153,204], [255,153,153], [255,153,102], [255,153,51], [255,153,0], [255,102,255], [255,102,204], [255,102,153], [255,102,102], [255,102,51], [255,102,0], [255,51,255], [255,51,204], [255,51,153], [255,51,102], [255,51,51], [255,51,0], [255,0,255], [255,0,204], [255,0,153], [255,0,102], [255,0,51], [255,0,0], [204,255,255], [204,255,204], [204,255,153], [204,255,102], [204,255,51], [204,255,0], [204,204,255], [204,204,204], [204,204,153], [204,204,102], [204,204,51], [204,204,0], [204,153,255], [204,153,204], [204,153,153], [204,153,102], [204,153,51], [204,153,0], [204,102,255], [204,102,204], [204,102,153], [204,102,102], [204,102,51], [204,102,0], [204,51,255], [204,51,204], [204,51,153], [204,51,102], [204,51,51], [204,51,0], [204,0,255], [204,0,204], [204,0,153], [204,0,102], [204,0,51], [204,0,0], [153,255,255], [153,255,204], [153,255,153], [153,255,102], [153,255,51], [153,255,0], [153,204,255], [153,204,204], [153,204,153], [153,204,102], [153,204,51], [153,204,0], [153,153,255], [153,153,204], [153,153,153], [153,153,102], [153,153,51], [153,153,0], [153,102,255], [153,102,204], [153,102,153], [153,102,102], [153,102,51], [153,102,0], [153,51,255], [153,51,204], [153,51,153], [153,51,102], [153,51,51], [153,51,0], [153,0,255], [153,0,204], [153,0,153], [153,0,102], [153,0,51], [153,0,0], [102,255,255], [102,255,204], [102,255,153], [102,255,102], [102,255,51], [102,255,0], [102,204,255], [102,204,204], [102,204,153], [102,204,102], [102,204,51], [102,204,0], [102,153,255], [102,153,204], [102,153,153], [102,153,102], [102,153,51], [102,153,0], [102,102,255], [102,102,204], [102,102,153], [102,102,102], [102,102,51], [102,102,0], [102,51,255], [102,51,204], [102,51,153], [102,51,102], [102,51,51], [102,51,0], [102,0,255], [102,0,204], [102,0,153], [102,0,102], [102,0,51], [102,0,0], [51,255,255], [51,255,204], [51,255,153], [51,255,102], [51,255,51], [51,255,0], [51,204,255], [51,204,204], [51,204,153], [51,204,102], [51,204,51], [51,204,0], [51,153,255], [51,153,204], [51,153,153], [51,153,102], [51,153,51], [51,153,0], [51,102,255], [51,102,204], [51,102,153], [51,102,102], [51,102,51], [51,102,0], [51,51,255], [51,51,204], [51,51,153], [51,51,102], [51,51,51], [51,51,0], [51,0,255], [51,0,204], [51,0,153], [51,0,102], [51,0,51], [51,0,0], [0,255,255], [0,255,204], [0,255,153], [0,255,102], [0,255,51], [0,255,0], [0,204,255], [0,204,204], [0,204,153], [0,204,102], [0,204,51], [0,204,0], [0,153,255], [0,153,204], [0,153,153], [0,153,102], [0,153,51], [0,153,0], [0,102,255], [0,102,204], [0,102,153], [0,102,102], [0,102,51], [0,102,0], [0,51,255], [0,51,204], [0,51,153], [0,51,102], [0,51,51], [0,51,0], [0,0,255], [0,0,204], [0,0,153], [0,0,102], [0,0,51], [238,0,0], [221,0,0], [187,0,0], [170,0,0], [136,0,0], [119,0,0], [85,0,0], [68,0,0], [34,0,0], [17,0,0], [0,238,0], [0,221,0], [0,187,0], [0,170,0], [0,136,0], [0,119,0], [0,85,0], [0,68,0], [0,34,0], [0,17,0], [0,0,238], [0,0,221], [0,0,187], [0,0,170], [0,0,136], [0,0,119], [0,0,85], [0,0,68], [0,0,34], [0,0,17], [238,238,238], [221,221,221], [187,187,187], [170,170,170], [136,136,136], [119,119,119], [85,85,85], [68,68,68], [34,34,34], [17,17,17], [0,0,0] ],
    win16: [ [0, 0, 0], [0, 0, 128], [0, 0, 255], [0, 128, 0], [0, 128, 128], [0, 255, 0], [0, 255, 255], [128, 0, 0],
        [128, 0, 128], [128, 128, 0], [128, 128, 128], [160, 160, 160], [255, 0, 0], [255, 0, 255], [255, 255, 0], [255, 255, 255] ],
    win256: [[0,0,0], [128,0,0], [0,128,0], [128,128,0], [0,0,128], [128,0,128], [0,128,128], [128,128,128], [192,220,192], [166,202,240], [42,63,170], [42,63,255], [42,95,0], [42,95,85], [42,95,170], [42,95,255], [42,127,0], [42,127,85], [42,127,170], [42,127,255], [42,159,0], [42,159,85], [42,159,170], [42,159,255], [42,191,0], [42,191,85], [42,191,170], [42,191,255], [42,223,0], [42,223,85], [42,223,170], [42,223,255], [42,255,0], [42,255,85], [42,255,170], [42,255,255], [85,0,0], [85,0,85], [85,0,170], [85,0,255], [85,31,0], [85,31,85], [85,31,255], [85,63,0], [85,63,0], [85,63,85], [85,63,170], [85,63,255], [85,95,0], [85,95,85], [85,95,170], [85,95,255], [85,127,0], [85,127,85], [85,127,170], [85,127,255], [85,159,0], [85,159,85], [85,159,170], [85,159,255], [85,191,0], [85,191,85], [85,191,170], [85,191,255], [85,223,0], [85,223,85], [85,223,170], [85,223,255], [85,255,0], [85,255,85], [85,255,170], [85,255,255], [127,0,0], [127,0,85], [127,0,170], [127,0,255], [127,31,0], [127,31,85], [127,31,170], [127,31,255], [127,63,0], [127,63,85], [127,63,170], [127,63,255], [127,95,0], [127,95,85], [127,95,170], [127,95,255], [127,127,0], [127,127,85], [127,127,170], [127,127,255], [127,159,0], [127,159,85], [127,159,170], [127,159,255], [127,191,0], [127,191,85], [127,191,170], [127,191,255], [127,223,0], [127,223,85], [127,223,170], [127,223,255], [127,255,0], [127,255,85], [127,255,170], [127,255,255], [170,0,0], [170,0,85], [170,0,170], [170,0,255], [170,31,0], [170,31,85], [170,31,170], [170,31,255], [170,63,0], [170,63,85], [170,63,170], [170,63,255], [170,95,0], [170,95,85], [170,95,170], [170,95,255], [170,127,0], [170,127,85], [170,127,170], [170,127,255], [170,159,0], [170,159,85], [170,159,170], [170,159,255], [170,191,0], [170,191,85], [170,191,170], [170,191,255], [170,223,0], [170,223,85], [170,223,170], [170,223,255], [170,255,0], [170,255,85], [170,255,170], [170,255,255], [212,0,0], [212,0,85], [212,0,170], [212,0,255], [212,31,0], [212,31,85], [212,31,170], [212,31,255], [212,63,0], [212,63,85], [212,63,170], [212,63,255], [212,95,0], [212,95,85], [212,95,170], [212,95,255], [212,127,0], [212,127,85], [212,127,170], [212,127,255], [212,159,0], [212,159,85], [212,159,170], [212,159,255], [212,191,0], [212,191,85], [212,191,170], [212,191,255], [212,223,0], [212,223,85], [212,223,170], [212,223,255], [212,255,0], [212,255,85], [212,255,170], [212,255,255], [255,0,85], [255,0,170], [255,31,0], [255,31,85], [255,31,170], [255,31,255], [255,63,0], [255,63,85], [255,63,170], [255,63,255], [255,95,0], [255,95,85], [255,95,170], [255,95,255], [255,127,0], [255,127,85], [255,127,170], [255,127,255], [255,159,0], [255,159,85], [255,159,170], [255,159,255], [255,191,0], [255,191,85], [255,191,170], [255,191,255], [255,223,0], [255,223,85], [255,223,170], [255,223,255], [255,255,85], [255,255,170], [204,204,255], [255,204,255], [51,255,255], [102,255,255], [153,255,255], [204,255,255], [0,127,0], [0,127,85], [0,127,170], [0,127,255], [0,159,0], [0,159,85], [0,159,170], [0,159,255], [0,191,0], [0,191,85], [0,191,170], [0,191,255], [0,223,0], [0,223,85], [0,223,170], [0,223,255], [0,255,85], [0,255,170], [42,0,0], [42,0,85], [42,0,170], [42,0,255], [42,31,0], [42,31,85], [42,31,170], [42,31,255], [42,63,0], [42,63,85], [255,251,240], [160,160,164], [128,128,128], [255,0,0], [0,255,0], [255,255,0], [0,0,255], [255,0,255], [0,255,255], [255,255,255]],
    uni8: [[255,255,255], [255,255,0], [255,0,255], [255,0,0], [0,255,255], [0,255,0], [0,0,255], [0,0,0]],
    uni27: [[255,255,255], [255,255,127], [255,255,0], [255,127,255], [255,127,127], [255,127,0], [255,0,255], [255,0,127], [255,0,0], [127,255,255], [127,255,127], [127,255,0], [127,127,255], [127,127,127], [127,127,0], [127,0,255], [127,0,127], [127,0,0], [0,255,255], [0,255,127], [0,255,0], [0,127,255], [0,127,127], [0,127,0], [0,0,255], [0,0,127], [0,0,0]],
    uni64: [[255,255,255], [255,255,170], [255,255,85], [255,255,0], [255,170,255], [255,170,170], [255,170,85], [255,170,0], [255,85,255], [255,85,170], [255,85,85], [255,85,0], [255,0,255], [255,0,170], [255,0,85], [255,0,0], [170,255,255], [170,255,170], [170,255,85], [170,255,0], [170,170,255], [170,170,170], [170,170,85], [170,170,0], [170,85,255], [170,85,170], [170,85,85], [170,85,0], [170,0,255], [170,0,170], [170,0,85], [170,0,0], [85,255,255], [85,255,170], [85,255,85], [85,255,0], [85,170,255], [85,170,170], [85,170,85], [85,170,0], [85,85,255], [85,85,170], [85,85,85], [85,85,0], [85,0,255], [85,0,170], [85,0,85], [85,0,0], [0,255,255], [0,255,170], [0,255,85], [0,255,0], [0,170,255], [0,170,170], [0,170,85], [0,170,0], [0,85,255], [0,85,170], [0,85,85], [0,85,0], [0,0,255], [0,0,170], [0,0,85], [0,0,0]],
    uni125: [[255,255,255], [255,255,191], [255,255,127], [255,255,64], [255,255,0], [255,191,255], [255,191,191], [255,191,127], [255,191,64], [255,191,0], [255,127,255], [255,127,191], [255,127,127], [255,127,64], [255,127,0], [255,64,255], [255,64,191], [255,64,127], [255,64,64], [255,64,0], [255,0,255], [255,0,191], [255,0,127], [255,0,64], [255,0,0], [191,255,255], [191,255,191], [191,255,127], [191,255,64], [191,255,0], [191,191,255], [191,191,191], [191,191,127], [191,191,64], [191,191,0], [191,127,255], [191,127,191], [191,127,127], [191,127,64], [191,127,0], [191,64,255], [191,64,191], [191,64,127], [191,64,64], [191,64,0], [191,0,255], [191,0,191], [191,0,127], [191,0,64], [191,0,0], [127,255,255], [127,255,191], [127,255,127], [127,255,64], [127,255,0], [127,191,255], [127,191,191], [127,191,127], [127,191,64], [127,191,0], [127,127,255], [127,127,191], [127,127,127], [127,127,64], [127,127,0], [127,64,255], [127,64,191], [127,64,127], [127,64,64], [127,64,0], [127,0,255], [127,0,191], [127,0,127], [127,0,64], [127,0,0], [64,255,255], [64,255,191], [64,255,127], [64,255,64], [64,255,0], [64,191,255], [64,191,191], [64,191,127], [64,191,64], [64,191,0], [64,127,255], [64,127,191], [64,127,127], [64,127,64], [64,127,0], [64,64,255], [64,64,191], [64,64,127], [64,64,64], [64,64,0], [64,0,255], [64,0,191], [64,0,127], [64,0,64], [64,0,0], [0,255,255], [0,255,191], [0,255,127], [0,255,64], [0,255,0], [0,191,255], [0,191,191], [0,191,127], [0,191,64], [0,191,0], [0,127,255], [0,127,191], [0,127,127], [0,127,64], [0,127,0], [0,64,255], [0,64,191], [0,64,127], [0,64,64], [0,64,0], [0,0,255], [0,0,191], [0,0,127], [0,0,64], [0,0,0]],
    uni216: [[255,255,255], [255,255,204], [255,255,153], [255,255,102], [255,255,51], [255,255,0], [255,204,255], [255,204,204], [255,204,153], [255,204,102], [255,204,51], [255,204,0], [255,153,255], [255,153,204], [255,153,153], [255,153,102], [255,153,51], [255,153,0], [255,102,255], [255,102,204], [255,102,153], [255,102,102], [255,102,51], [255,102,0], [255,51,255], [255,51,204], [255,51,153], [255,51,102], [255,51,51], [255,51,0], [255,0,255], [255,0,204], [255,0,153], [255,0,102], [255,0,51], [255,0,0], [204,255,255], [204,255,204], [204,255,153], [204,255,102], [204,255,51], [204,255,0], [204,204,255], [204,204,204], [204,204,153], [204,204,102], [204,204,51], [204,204,0], [204,153,255], [204,153,204], [204,153,153], [204,153,102], [204,153,51], [204,153,0], [204,102,255], [204,102,204], [204,102,153], [204,102,102], [204,102,51], [204,102,0], [204,51,255], [204,51,204], [204,51,153], [204,51,102], [204,51,51], [204,51,0], [204,0,255], [204,0,204], [204,0,153], [204,0,102], [204,0,51], [204,0,0], [153,255,255], [153,255,204], [153,255,153], [153,255,102], [153,255,51], [153,255,0], [153,204,255], [153,204,204], [153,204,153], [153,204,102], [153,204,51], [153,204,0], [153,153,255], [153,153,204], [153,153,153], [153,153,102], [153,153,51], [153,153,0], [153,102,255], [153,102,204], [153,102,153], [153,102,102], [153,102,51], [153,102,0], [153,51,255], [153,51,204], [153,51,153], [153,51,102], [153,51,51], [153,51,0], [153,0,255], [153,0,204], [153,0,153], [153,0,102], [153,0,51], [153,0,0], [102,255,255], [102,255,204], [102,255,153], [102,255,102], [102,255,51], [102,255,0], [102,204,255], [102,204,204], [102,204,153], [102,204,102], [102,204,51], [102,204,0], [102,153,255], [102,153,204], [102,153,153], [102,153,102], [102,153,51], [102,153,0], [102,102,255], [102,102,204], [102,102,153], [102,102,102], [102,102,51], [102,102,0], [102,51,255], [102,51,204], [102,51,153], [102,51,102], [102,51,51], [102,51,0], [102,0,255], [102,0,204], [102,0,153], [102,0,102], [102,0,51], [102,0,0], [51,255,255], [51,255,204], [51,255,153], [51,255,102], [51,255,51], [51,255,0], [51,204,255], [51,204,204], [51,204,153], [51,204,102], [51,204,51], [51,204,0], [51,153,255], [51,153,204], [51,153,153], [51,153,102], [51,153,51], [51,153,0], [51,102,255], [51,102,204], [51,102,153], [51,102,102], [51,102,51], [51,102,0], [51,51,255], [51,51,204], [51,51,153], [51,51,102], [51,51,51], [51,51,0], [51,0,255], [51,0,204], [51,0,153], [51,0,102], [51,0,51], [51,0,0], [0,255,255], [0,255,204], [0,255,153], [0,255,102], [0,255,51], [0,255,0], [0,204,255], [0,204,204], [0,204,153], [0,204,102], [0,204,51], [0,204,0], [0,153,255], [0,153,204], [0,153,153], [0,153,102], [0,153,51], [0,153,0], [0,102,255], [0,102,204], [0,102,153], [0,102,102], [0,102,51], [0,102,0], [0,51,255], [0,51,204], [0,51,153], [0,51,102], [0,51,51], [0,51,0], [0,0,255], [0,0,204], [0,0,153], [0,0,102], [0,0,51], [0,0,0]]
};

// Copy a palette to avoid accidental mutation.
CG._copyPalette = function (palette) {
    var out = [];
    for (var i = 0; i < palette.length; i++) {
        var c = palette[i];
        out[i] = [c[0], c[1], c[2]];
    }
    return out;
};

// Evenly limit a palette to a target size (keeps endpoints).
CG._limitPalette = function (palette, colors) {
    if (palette.length <= colors) return CG._copyPalette(palette);
    var out = [];
    if (colors <= 1) return out;
    var step = (palette.length - 1) / (colors - 1);
    for (var i = 0; i < colors; i++) {
        var idx = Math.round(i * step);
        out[i] = palette[idx];
    }
    return CG._copyPalette(out);
};

// Build a uniform palette with a best-effort size match.
CG._uniformPalette = function (colors) {
    var key = "uni" + colors;
    if (CG.rgbQuantPaletteData[key]) return CG._copyPalette(CG.rgbQuantPaletteData[key]);
    if (colors <= 2) return [[0, 0, 0], [255, 255, 255]];
    var steps = Math.round(Math.pow(colors, 1 / 3));
    if (steps < 2) steps = 2;
    var values = [];
    for (var i = 0; i < steps; i++) {
        values[i] = Math.round(255 * i / (steps - 1));
    }
    var pal = [];
    for (var r = 0; r < steps; r++) {
        for (var g = 0; g < steps; g++) {
            for (var b = 0; b < steps; b++) {
                pal[pal.length] = [values[r], values[g], values[b]];
            }
        }
    }
    if (pal.length > colors) return CG._limitPalette(pal, colors);
    if (pal.length < colors) {
        var missing = colors - pal.length;
        for (var i = 0; i < missing; i++) {
            var v = Math.round(255 * i / Math.max(1, missing - 1));
            pal[pal.length] = [v, v, v];
        }
    }
    return pal;
};

// Collect unique RGB colors from an image (up to limit+1).
CG._collectUniquePalette = function (img, limit) {
    var out = [];
    var seen = {};
    var src = img.data;
    var len = Buffer.size(src);
    for (var i = 0; i < len; i += 4) {
        if (src[i + 3] === 0) continue;
        var r = src[i];
        var g = src[i + 1];
        var b = src[i + 2];
        var key = r | (g << 8) | (b << 16);
        if (!seen[key]) {
            seen[key] = 1;
            out[out.length] = [r, g, b];
            if (out.length > limit) break;
        }
    }
    return out;
};

CG._isOrderedDither = function (kernel) {
    return kernel == "Ordered2" || kernel == "Ordered3" || kernel == "Ordered4" || kernel == "Ordered8";
};

CG._rgbQuantReduce = function (rq, img, dithKern, dithSerp) {
    CG._debugLog("rgbQuantReduce start " + img.width + "x" + img.height +
        " dither=" + (dithKern || "") + " serp=" + (dithSerp ? "1" : "0"));
    var out = rq.reduce(img, "image", dithKern, dithSerp);
    CG._debugLog("rgbQuantReduce image done");
    return out;
};

CG._sampleForQuant = function (img, maxDim) {
    if (!maxDim || maxDim <= 0) return img;
    if (img.width <= maxDim && img.height <= maxDim) return img;
    var scale = Math.min(maxDim / img.width, maxDim / img.height);
    var w = Math.round(img.width * scale);
    var h = Math.round(img.height * scale);
    CG._debugLog("sampleForQuant resample " + img.width + "x" + img.height + " -> " + w + "x" + h);
    return Image.resample(img, w, h, "linear");
};

// Build RgbQuant options for ElectricCat palette presets.
CG._rgbQuantOptions = function (img, preset, colors, dithKern, dithSerp) {
    if (!colors || colors < 2) colors = 2;
    if (colors > 256) colors = 256;
    var opts = {
        colors: colors,
        method: 2,
        boxSize: [40, 40],
        boxPxls: 3,
        dithKern: dithKern || null,
        dithSerp: !!dithSerp,
        palette: null,
        reIndex: false
    };
    if (preset === "ada" || !preset) return opts;
    if (preset === "exa") {
        var pal = CG._collectUniquePalette(img, colors);
        if (pal.length <= colors) {
            opts.palette = pal;
            opts.colors = pal.length;
        }
        return opts;
    }
    if (preset === "bitmap") {
        opts.palette = [[0, 0, 0], [255, 255, 255]];
        opts.colors = 2;
        return opts;
    }
    if (preset === "uni") {
        opts.palette = CG._uniformPalette(colors);
        opts.colors = opts.palette.length;
        return opts;
    }
    if (preset === "mac") {
        opts.palette = CG._limitPalette(CG.rgbQuantPaletteData.mac, colors);
        opts.colors = opts.palette.length;
        return opts;
    }
    if (preset === "web") {
        opts.palette = CG._limitPalette(CG.rgbQuantPaletteData.web, colors);
        opts.colors = opts.palette.length;
        return opts;
    }
    if (preset === "win") {
        var base = (colors <= 16) ? CG.rgbQuantPaletteData.win16 : CG.rgbQuantPaletteData.win256;
        opts.palette = CG._limitPalette(base, colors);
        opts.colors = opts.palette.length;
        return opts;
    }
    return opts;
};

// Apply RgbQuant with raw options.
CG.rgbQuantize = function (img, opts) {
    CG._debugLog("rgbQuantize raw opts");
    var rq = new RgbQuant(opts || {});
    rq.sample(img);
    return CG._rgbQuantReduce(rq, img, opts ? opts.dithKern : null, opts ? opts.dithSerp : false);
};

// Apply RgbQuant using ElectricCat-style preset names.
CG.rgbQuantizePreset = function (img, preset, colors, dithKern, dithSerp, sampleMax, workMax) {
    var opts = CG._rgbQuantOptions(img, preset, colors, dithKern, dithSerp);
    CG._debugLog("rgbQuantizePreset preset=" + preset + " colors=" + colors +
        " dither=" + (dithKern || "") + " serp=" + (dithSerp ? "1" : "0"));
    var rq = new RgbQuant(opts);
    var sampleLimit = sampleMax;
    if (sampleLimit === undefined || sampleLimit === null) {
        sampleLimit = 0;
    }
    if (preset === "exa") sampleLimit = 0;
    var workLimit = workMax;
    if (workLimit === undefined || workLimit === null) {
        workLimit = 0;
    }
    var sampleImg = CG._sampleForQuant(img, sampleLimit);
    CG._debugLog("sampleImg " + sampleImg.width + "x" + sampleImg.height + " sampleMax=" + sampleLimit);
    rq.sample(sampleImg);
    var workImg = CG._sampleForQuant(img, workLimit);
    CG._debugLog("workImg " + workImg.width + "x" + workImg.height + " workMax=" + workLimit);
    var out = CG._rgbQuantReduce(rq, workImg, opts.dithKern, opts.dithSerp);
    if (out.width != img.width || out.height != img.height) {
        out = Image.resample(out, img.width, img.height, "none");
    }
    return out;
};

// Create a blank RGBA8 image. width/height in pixels, >0 integers.
CG.createImage = function (width, height) {
    var img = {};
    img.width = width;
    img.height = height;
    img.data = Buffer.alloc(width * height * 4);
    return img;
};

// Clone an image (deep copy of data). img must be {width,height,data}.
CG.cloneImage = function (img) {
    var out = CG.createImage(img.width, img.height);
    var src = img.data;
    var dst = out.data;
    var len = Buffer.size(src);
    for (var i = 0; i < len; i++) {
        dst[i] = src[i];
    }
    return out;
};

// Clamp scalar to [0,255].
CG._clamp255 = function (v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
};

// Get clamped pixel index for (x,y). x/y in pixels.
CG._getIndexClamp = function (x, y, width, height) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= width) x = width - 1;
    if (y >= height) y = height - 1;
    return (y * width + x) * 4;
};

// Bilinear sample src at (x,y) and write RGBA to out[outIndex].
// x/y in pixel space (float), width/height in pixels.
CG._sampleBilinear = function (src, width, height, x, y, out, outIndex) {
    var fx = x < 0 ? (x - 1) | 0 : x | 0;
    var fy = y < 0 ? (y - 1) | 0 : y | 0;
    var wx = x - fx;
    var wy = y - fy;
    var i0 = CG._getIndexClamp(fx, fy, width, height);
    var i1 = CG._getIndexClamp(fx + 1, fy, width, height);
    var i2 = CG._getIndexClamp(fx, fy + 1, width, height);
    var i3 = CG._getIndexClamp(fx + 1, fy + 1, width, height);
    var cx = 1 - wx;
    var cy = 1 - wy;
    var r = (src[i0] * cx + src[i1] * wx) * cy + (src[i2] * cx + src[i3] * wx) * wy;
    var g = (src[i0 + 1] * cx + src[i1 + 1] * wx) * cy + (src[i2 + 1] * cx + src[i3 + 1] * wx) * wy;
    var b = (src[i0 + 2] * cx + src[i1 + 2] * wx) * cy + (src[i2 + 2] * cx + src[i3 + 2] * wx) * wy;
    var a = (src[i0 + 3] * cx + src[i1 + 3] * wx) * cy + (src[i2 + 3] * cx + src[i3 + 3] * wx) * wy;
    out[outIndex] = CG._clamp255(r | 0);
    out[outIndex + 1] = CG._clamp255(g | 0);
    out[outIndex + 2] = CG._clamp255(b | 0);
    out[outIndex + 3] = CG._clamp255(a | 0);
};

// Convert RGB (0-255) to HSL array [h,s,l] with h in [0,1].
CG._rgbToHsl = function (r, g, b) {
    r /= 255;
    g /= 255;
    b /= 255;
    var max = (r > g) ? (r > b) ? r : b : (g > b) ? g : b;
    var min = (r < g) ? (r < b) ? r : b : (g < b) ? g : b;
    var chroma = max - min;
    var h = 0;
    var s = 0;
    var l = (min + max) / 2;
    if (chroma !== 0) {
        if (r === max) {
            h = (g - b) / chroma + ((g < b) ? 6 : 0);
        } else if (g === max) {
            h = (b - r) / chroma + 2;
        } else {
            h = (r - g) / chroma + 4;
        }
        h /= 6;
        s = (l > 0.5) ? chroma / (2 - max - min) : chroma / (max + min);
    }
    return [h, s, l];
};

// Helper for HSL->RGB. h in [0,1], m1/m2 in [0,1].
CG._hueToRgb = function (m1, m2, h) {
    if (h < 0) h += 1;
    else if (h > 1) h -= 1;
    if (6 * h < 1) return m1 + (m2 - m1) * h * 6;
    if (2 * h < 1) return m2;
    if (3 * h < 2) return m1 + (m2 - m1) * (2 / 3 - h) * 6;
    return m1;
};

// Convert HSL (h,s,l in [0,1]) to RGB array [r,g,b] in 0-255.
CG._hslToRgb = function (h, s, l) {
    var m1, m2;
    if (s === 0) {
        var v = (l * 255 + 0.5) | 0;
        return [v, v, v];
    }
    m2 = (l <= 0.5) ? l * (1 + s) : l + s - l * s;
    m1 = 2 * l - m2;
    var r = CG._hueToRgb(m1, m2, h + 1 / 3);
    var g = CG._hueToRgb(m1, m2, h);
    var b = CG._hueToRgb(m1, m2, h - 1 / 3);
    return [(r * 255 + 0.5) | 0, (g * 255 + 0.5) | 0, (b * 255 + 0.5) | 0];
};

// Convert RGB (0-255) to HSL object {h,s,l} with h in [0,6).
CG._tRGBToHSL = function (r, g, b) {
    var max, min, delta, h, s, l;
    r = r / 255;
    g = g / 255;
    b = b / 255;
    max = Math.max(Math.max(r, g), b);
    min = Math.min(Math.min(r, g), b);
    l = (max + min) / 2;
    if (max === min) {
        s = 0;
        h = 0;
    } else {
        delta = max - min;
        if (l <= 0.5) s = delta / (max + min);
        else s = delta / (2 - max - min);
        if (r === max) h = (g - b) / delta;
        else if (g === max) h = 2 + (b - r) / delta;
        else h = 4 + (r - g) / delta;
    }
    h = h * 60;
    if (h < 0) h += 360;
    return { h: h / 60, s: s, l: l };
};

// Convert HSL (h in [0,6), s/l in [0,1]) to RGB object.
CG._tHSLToRGB = function (h, s, l) {
    var r = 0;
    var g = 0;
    var b = 0;
    var Min, Max, h1;
    if (s === 0) {
        r = g = b = l;
    } else {
        if (l < 0.5) Max = l * (1 + s);
        else Max = l + s - l * s;
        Min = 2 * l - Max;
        h1 = h;
        if (h1 < 1) {
            r = Max;
            if (h1 < 0) {
                g = Min;
                b = g - h1 * (Max - Min);
            } else {
                b = Min;
                g = h1 * (Max - Min) + b;
            }
        } else if (h1 < 3) {
            g = Max;
            if (h1 < 2) {
                b = Min;
                r = b - (h1 - 2) * (Max - Min);
            } else {
                r = Min;
                b = (h1 - 2) * (Max - Min) + r;
            }
        } else {
            b = Max;
            if (h1 < 4) {
                r = Min;
                g = r - (h1 - 4) * (Max - Min);
            } else {
                g = Min;
                r = (h1 - 4) * (Max - Min) + g;
            }
        }
    }
    r *= 255;
    g *= 255;
    b *= 255;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return { r: r, g: g, b: b };
};

// Compute Rec.709 luminance from RGB (0-255), returns 0-255.
CG._getLuminance = function (r, g, b) {
    return Math.round(r * 0.2126 + g * 0.7152 + b * 0.0722);
};

// Build 256-entry LUT from function f(value).
CG._buildMap = function (f) {
    var map = [];
    for (var k = 0; k < 256; k += 1) {
        var v = f(k);
        map[k] = (v > 255) ? 255 : (v < 0) ? 0 : v | 0;
    }
    return map;
};

// Build LUT from curve points [[x,y],...], x/y in 0-255.
CG._createLUTFromCurve = function (points) {
    var lut = [];
    var p = [0, 0];
    var j = 0;
    for (var i = 0; i < 256; i++) {
        while (j < points.length && points[j][0] < i) {
            p = points[j];
            j++;
        }
        lut[i] = p[1];
    }
    return lut;
};

// Apply per-channel LUTs. Each LUT length 256.
CG._applyLUT = function (img, lutR, lutG, lutB) {
    var out = CG.cloneImage(img);
    var d = out.data;
    var len = Buffer.size(d);
    for (var i = 0; i < len; i += 4) {
        d[i] = lutR[d[i]];
        d[i + 1] = lutG[d[i + 1]];
        d[i + 2] = lutB[d[i + 2]];
    }
    return out;
};

// Luminance histogram. Returns array[256] counts.
CG.histogram = function (img) {
    var hist = [];
    for (var i = 0; i < 256; i++) hist[i] = 0;
    var src = img.data;
    var len = Buffer.size(src);
    for (var i = 0; i < len; i += 4) {
        var v = CG._getLuminance(src[i], src[i + 1], src[i + 2]);
        hist[v] += 1;
    }
    return hist;
};

// Per-channel histogram. Returns [R,G,B] arrays[256].
CG.histogramChannels = function (img) {
    var hist = [[], [], []];
    for (var c = 0; c < 3; c++) {
        for (var i = 0; i < 256; i++) hist[c][i] = 0;
    }
    var src = img.data;
    var len = Buffer.size(src);
    for (var i = 0; i < len; i += 4) {
        hist[0][src[i]] += 1;
        hist[1][src[i + 1]] += 1;
        hist[2][src[i + 2]] += 1;
    }
    return hist;
};

// Normalize RGB to full range based on min/max. No params.
CG.normalize = function (img) {
    var out = CG.cloneImage(img);
    var d = out.data;
    var len = Buffer.size(d);
    var maxv = 0;
    var minv = 255;
    for (var i = 0; i < len; i += 4) {
        for (var c = 0; c < 3; c++) {
            var v = d[i + c];
            if (v > maxv) maxv = v;
            if (v < minv) minv = v;
        }
    }
    var range = maxv - minv;
    var lut = [];
    for (var i = 0; i < 256; i++) lut[i] = 0;
    if (range !== 0) {
        for (var i = minv; i <= maxv; i++) {
            lut[i] = 255 * (i - minv) / range;
        }
    } else {
        lut[minv] = minv;
    }
    for (var i = 0; i < len; i += 4) {
        d[i] = lut[d[i]];
        d[i + 1] = lut[d[i + 1]];
        d[i + 2] = lut[d[i + 2]];
    }
    return out;
};

// Equalize using luminance histogram. No params.
CG.equalize = function (img) {
    var out = CG.cloneImage(img);
    var d = out.data;
    var len = Buffer.size(d);
    var hist = CG.histogram(out);
    var pixels = 0;
    for (var i = 0; i < 256; i++) pixels += hist[i];
    var lut = [];
    var sum = 0;
    for (var i = 0; i < 256; i++) {
        sum += hist[i];
        lut[i] = Math.floor(sum * 255 / pixels + 0.5);
    }
    for (var i = 0; i < len; i += 4) {
        d[i] = lut[d[i]];
        d[i + 1] = lut[d[i + 1]];
        d[i + 2] = lut[d[i + 2]];
    }
    return out;
};

// Equalize each RGB channel independently. No params.
CG.equalizeChannels = function (img) {
    var out = CG.cloneImage(img);
    var d = out.data;
    var len = Buffer.size(d);
    var hists = CG.histogramChannels(out);
    var luts = [[], [], []];
    for (var c = 0; c < 3; c++) {
        var pixels = 0;
        var sum = 0;
        for (var i = 0; i < 256; i++) pixels += hists[c][i];
        for (var i = 0; i < 256; i++) {
            sum += hists[c][i];
            luts[c][i] = Math.floor(sum * 255 / pixels + 0.5);
        }
    }
    for (var i = 0; i < len; i += 4) {
        d[i] = luts[0][d[i]];
        d[i + 1] = luts[1][d[i + 1]];
        d[i + 2] = luts[2][d[i + 2]];
    }
    return out;
};

// Invert RGB channels. No params.
CG.invert = function (img) {
    var out = CG.cloneImage(img);
    var d = out.data;
    var len = Buffer.size(d);
    for (var i = 0; i < len; i += 4) {
        d[i] = 255 - d[i];
        d[i + 1] = 255 - d[i + 1];
        d[i + 2] = 255 - d[i + 2];
    }
    return out;
};

// Desaturate to luminance grayscale. No params.
CG.desaturate = function (img) {
    var out = CG.cloneImage(img);
    var d = out.data;
    var len = Buffer.size(d);
    for (var i = 0; i < len; i += 4) {
        var v = CG._getLuminance(d[i], d[i + 1], d[i + 2]);
        d[i] = v;
        d[i + 1] = v;
        d[i + 2] = v;
    }
    return out;
};

// Alias for desaturate().
CG.grayscale = CG.desaturate;

// Threshold to high/low by luminance. threshold/high/low in [0,255].
CG.threshold = function (img, threshold, high, low) {
    if (high === null || typeof high === "undefined") high = 255;
    if (low === null || typeof low === "undefined") low = 0;
    var out = CG.cloneImage(img);
    var d = out.data;
    var len = Buffer.size(d);
    for (var i = 0; i < len; i += 4) {
        var v = (0.3 * d[i] + 0.59 * d[i + 1] + 0.11 * d[i + 2] >= threshold) ? high : low;
        d[i] = v;
        d[i + 1] = v;
        d[i + 2] = v;
    }
    return out;
};

// Posterize to N levels. levels in [2,255].
CG.posterize = function (img, levels) {
    if (levels < 2) levels = 2;
    if (levels > 255) levels = 255;
    var out = CG.cloneImage(img);
    var d = out.data;
    var len = Buffer.size(d);
    var levelMap = [];
    var levelsMinus1 = levels - 1;
    for (var i = 0; i < levels; i++) {
        levelMap[i] = (255 * i) / levelsMinus1;
    }
    var j = 0;
    var k = 0;
    var map = CG._buildMap(function () {
        var ret = levelMap[j];
        k += levels;
        if (k > 255) {
            k -= 255;
            j += 1;
        }
        return ret;
    });
    for (var i = 0; i < len; i += 4) {
        d[i] = map[d[i]];
        d[i + 1] = map[d[i + 1]];
        d[i + 2] = map[d[i + 2]];
    }
    return out;
};

// Floyd-Steinberg dither using N levels. levels in [2,255].
CG.dither = function (img, levels) {
    var out = CG.cloneImage(img);
    var d = out.data;
    var width = out.width;
    var height = out.height;
    if (levels < 2) levels = 2;
    if (levels > 255) levels = 255;
    var levelMap = [];
    var levelsMinus1 = levels - 1;
    for (var i = 0; i < levels; i++) {
        levelMap[i] = (255 * i) / levelsMinus1;
    }
    var j = 0;
    var k = 0;
    var posterize = CG._buildMap(function () {
        var ret = levelMap[j];
        k += levels;
        if (k > 255) {
            k -= 255;
            j += 1;
        }
        return ret;
    });
    var A = 7 / 16;
    var B = 3 / 16;
    var C = 5 / 16;
    var D = 1 / 16;
    for (var y = 0; y < height; y += 1) {
        for (var x = 0; x < width; x += 1) {
            var index = (y * width + x) * 4;
            var old_r = d[index];
            var old_g = d[index + 1];
            var old_b = d[index + 2];
            var new_r = posterize[old_r];
            var new_g = posterize[old_g];
            var new_b = posterize[old_b];
            d[index] = new_r;
            d[index + 1] = new_g;
            d[index + 2] = new_b;
            var err_r = old_r - new_r;
            var err_g = old_g - new_g;
            var err_b = old_b - new_b;
            if (x + 1 < width) {
                var i1 = index + 4;
                d[i1] = CG._clamp255(d[i1] + err_r * A);
                d[i1 + 1] = CG._clamp255(d[i1 + 1] + err_g * A);
                d[i1 + 2] = CG._clamp255(d[i1 + 2] + err_b * A);
            }
            if (y + 1 < height) {
                if (x > 0) {
                    var i2 = index + (width - 1) * 4;
                    d[i2] = CG._clamp255(d[i2] + err_r * B);
                    d[i2 + 1] = CG._clamp255(d[i2 + 1] + err_g * B);
                    d[i2 + 2] = CG._clamp255(d[i2 + 2] + err_b * B);
                }
                var i3 = index + width * 4;
                d[i3] = CG._clamp255(d[i3] + err_r * C);
                d[i3 + 1] = CG._clamp255(d[i3 + 1] + err_g * C);
                d[i3 + 2] = CG._clamp255(d[i3 + 2] + err_b * C);
                if (x + 1 < width) {
                    var i4 = index + (width + 1) * 4;
                    d[i4] = CG._clamp255(d[i4] + err_r * D);
                    d[i4 + 1] = CG._clamp255(d[i4 + 1] + err_g * D);
                    d[i4 + 2] = CG._clamp255(d[i4 + 2] + err_b * D);
                }
            }
        }
    }
    return out;
};

// Brightness/contrast. brightness and contrast are floats, typical range [-1,1].
CG.brightnessContrast = function (img, brightness, contrast) {
    var out = CG.cloneImage(img);
    var d = out.data;
    var len = Buffer.size(d);
    for (var i = 0; i < len; i += 4) {
        d[i] = Math.round((((d[i] / 255) - 0.5) * contrast + brightness + 0.5) * 255);
        d[i + 1] = Math.round((((d[i + 1] / 255) - 0.5) * contrast + brightness + 0.5) * 255);
        d[i + 2] = Math.round((((d[i + 2] / 255) - 0.5) * contrast + brightness + 0.5) * 255);
        d[i] = CG._clamp255(d[i]);
        d[i + 1] = CG._clamp255(d[i + 1]);
        d[i + 2] = CG._clamp255(d[i + 2]);
    }
    return out;
};

// Brightness/contrast via LUT. brightness/contrast are floats, typical [-1,1].
CG.brightnessContrastLUT = function (img, brightness, contrast) {
    var contrastAdjust = -128 * contrast + 128;
    var brightnessAdjust = 255 * brightness;
    var adjust = contrastAdjust + brightnessAdjust;
    var lut = [];
    for (var i = 0; i < 256; i++) {
        var c = i * contrast + adjust;
        lut[i] = (c < 0) ? 0 : (c > 255 ? 255 : c);
    }
    return CG._applyLUT(img, lut, lut, lut);
};

// Curves per channel. pointsR/G/B are arrays of [x,y] in 0-255.
CG.curves = function (img, pointsR, pointsG, pointsB) {
    var lutR = CG._createLUTFromCurve(pointsR);
    var lutG = pointsG ? CG._createLUTFromCurve(pointsG) : lutR;
    var lutB = pointsB ? CG._createLUTFromCurve(pointsB) : lutR;
    return CG._applyLUT(img, lutR, lutG, lutB);
};

// Levels adjustment. levels is array of channel configs with Input/Output.
CG.levels = function (img, levels) {
    var out = CG.cloneImage(img);
    var d = out.data;
    var len = Buffer.size(d);
    var defaults = {
        Input: { Low: 0, High: 255, Gamma: 1.0 },
        Output: { Low: 0, High: 255 }
    };
    var l = [levels[0] || defaults, levels[1] || defaults, levels[2] || defaults];
    for (var i = 0; i < len; i += 4) {
        for (var c = 0; c < 3; c++) {
            var v = d[i + c];
            var inLow = l[c].Input.Low;
            var inHigh = l[c].Input.High;
            var gamma = l[c].Input.Gamma;
            var outLow = l[c].Output.Low;
            var outHigh = l[c].Output.High;
            var inten = 0;
            if (inHigh !== inLow) {
                inten = (v - inLow) / (inHigh - inLow);
            } else {
                inten = v - inLow;
            }
            if (gamma !== 0) {
                inten = Math.pow(inten, 1.0 / gamma);
            }
            if (outHigh >= outLow) {
                inten = (inten * (outHigh - outLow) + outLow);
            } else {
                inten = (outLow - inten * (outLow - outHigh));
            }
            d[i + c] = CG._clamp255(inten);
        }
    }
    return out;
};

// Hue/Saturation/Lightness. h in degrees, s/l in percent; colorize boolean.
CG.hueSaturation = function (img, h, s, l, colorize) {
    var out = CG.cloneImage(img);
    var d = out.data;
    var len = Buffer.size(d);
    h = parseFloat(h) / 60.0;
    if (colorize) h = h + 3;
    s = (parseFloat(s) + 100.0) / 100.0;
    l = parseFloat(l) / 100.0;
    for (var i = 0; i < len; i += 4) {
        var rgb = CG._tRGBToHSL(d[i], d[i + 1], d[i + 2]);
        rgb.h = rgb.h + h;
        if (rgb.h > 5) rgb.h = rgb.h - 6;
        if (rgb.h < -1) rgb.h = rgb.h + 6;
        rgb.s = rgb.s * s;
        if (rgb.s < 0) rgb.s = 0;
        if (rgb.s > 1) rgb.s = 1;
        rgb.l = rgb.l + l;
        if (rgb.l < 0) rgb.l = 0;
        if (rgb.l > 1) rgb.l = 1;
        var outRGB = colorize ? CG._tHSLToRGB(h, s, rgb.l) : CG._tHSLToRGB(rgb.h, rgb.s, rgb.l);
        d[i] = outRGB.r;
        d[i + 1] = outRGB.g;
        d[i + 2] = outRGB.b;
    }
    return out;
};

// Vibrance adjustment. amount float, typical range [-1,1].
CG.vibrance = function (img, amount) {
    var out = CG.cloneImage(img);
    var d = out.data;
    var len = Buffer.size(d);
    for (var i = 0; i < len; i += 4) {
        var r = d[i];
        var g = d[i + 1];
        var b = d[i + 2];
        var avg = (r + g + b) / 3;
        var mx = Math.max(r, Math.max(g, b));
        var amt = (mx - avg) * (-amount * 3.0) / 255.0;
        d[i] = CG._clamp255(r + (mx - r) * amt);
        d[i + 1] = CG._clamp255(g + (mx - g) * amt);
        d[i + 2] = CG._clamp255(b + (mx - b) * amt);
    }
    return out;
};

// Kodak/Wratten photo filter presets: [code, name, description, [r,g,b]].
CG.photoFilterPresets = [
    ["1A", "skylight (pale pink)", "reduce haze in landscape photography", [245, 236, 240]],
    ["2A", "pale yellow", "absorb UV radiation", [244, 243, 233]],
    ["2B", "pale yellow", "absorb UV radiation (slightly less than 2A)", [244, 245, 230]],
    ["2E", "pale yellow", "absorb UV radiation (slightly more than 2A)", [242, 254, 139]],
    ["3", "light yellow", "absorb excessive sky blue, make sky darker in black/white photos", [255, 250, 110]],
    ["6", "light yellow", "absorb excessive sky blue, emphasizing clouds", [253, 247, 3]],
    ["8", "yellow", "high blue absorption; correction for sky, cloud, and foliage", [247, 241, 0]],
    ["9", "deep yellow", "moderate contrast in black/white outdoor photography", [255, 228, 0]],
    ["11", "yellow-green", "correction for tungsten light", [75, 175, 65]],
    ["12", "deep yellow", "minus blue; reduce haze in aerial photos", [255, 220, 0]],
    ["15", "deep yellow", "darken sky in black/white outdoor photography", [240, 160, 50]],
    ["16", "yellow-orange", "stronger version of 15", [237, 140, 20]],
    ["21", "orange", "contrast filter for blue and blue-green absorption", [245, 100, 50]],
    ["22", "deep orange", "stronger version of 21", [247, 84, 33]],
    ["23A", "light red", "contrast effects, darken sky and water", [255, 117, 106]],
    ["24", "red", "red for two-color photography (daylight or tungsten)", [240, 0, 0]],
    ["25", "red", "tricolor red; contrast effects in outdoor scenes", [220, 0, 60]],
    ["26", "red", "stereo red; cuts haze, useful for storm or moonlight settings", [210, 0, 0]],
    ["29", "deep red", "color separation; extreme sky darkening in black/white photos", [115, 10, 25]],
    ["32", "magenta", "green absorption", [240, 0, 255]],
    ["33", "strong green absorption", "variant on 32", [154, 0, 78]],
    ["34A", "violet", "minus-green and plus-blue separation", [124, 40, 240]],
    ["38A", "blue", "red absorption; useful for contrast in microscopy", [1, 156, 210]],
    ["44", "light blue-green", "minus-red, two-color general viewing", [0, 136, 152]],
    ["44A", "light blue-green", "minus-red, variant on 44", [0, 175, 190]],
    ["47", "blue tricolor", "direct color separation; contrast effects in commercial photography", [43, 75, 220]],
    ["47A", "light blue", "enhance blue and purple objects; useful for fluorescent dyes", [0, 15, 150]],
    ["47B", "deep blue tricolor", "color separation; calibration using SMPTE color bars", [0, 0, 120]],
    ["56", "very light green", "darkens sky, improves flesh tones", [132, 206, 35]],
    ["58", "green tricolor", "used for color separation; improves definition of foliage", [40, 110, 5]],
    ["61", "deep green tricolor", "used for color separation, tungsten tricolor projection", [40, 70, 10]],
    ["70", "dark red", "infrared photography longpass filter blocking", [62, 0, 0]],
    ["80A", "blue", "cooling filter, 3200K to 5500K; converts indoor lighting to sunlight", [50, 100, 230]],
    ["80B", "blue", "variant of 80A, 3400K to 5500K", [70, 120, 230]],
    ["80C", "blue", "variant of 80A, 3800K to 5500K", [90, 140, 235]],
    ["80D", "blue", "variant of 80A, 4200K to 5500K", [110, 160, 240]],
    ["81A", "pale orange", "warming filter (lowers color temperature), 3400 K to 3200 K", [247, 240, 220]],
    ["81B", "pale orange", "warming filter; slightly stronger than 81A", [242, 232, 205]],
    ["81C", "pale orange", "warming filter; slightly stronger than 81B", [230, 220, 200]],
    ["81D", "pale orange", "warming filter; slightly stronger than 81C", [235, 220, 190]],
    ["81EF", "pale orange", "warming filter; slightly stronger than 81D", [215, 185, 150]],
    ["82", "blue", "cooling filter; raises color temperature 100K", [150, 205, 240]],
    ["82A", "pale blue", "cooling filter; opposite of 81A", [205, 225, 235]],
    ["82B", "pale blue", "cooling filter; opposite of 81B", [155, 190, 220]],
    ["82C", "pale blue", "cooling filter; opposite of 81C", [120, 155, 190]],
    ["85", "amber", "warming filter, 5500K to 3400K; converts sunlight to incandescent", [250, 155, 115]],
    ["85B", "amber", "warming filter, 5500K to 3200K; opposite of 80A", [250, 125, 95]],
    ["85C", "amber", "warming filter, 5500K to 3800K; opposite of 80C", [250, 155, 115]],
    ["90", "dark gray amber", "remove color before photographing; rarely used for actual photos", [100, 85, 20]],
    ["96", "neutral gray", "neutral density filter; equally blocks all light frequencies", [100, 100, 100]]
];

// Photo filter overlay. density 0-100, rgb array [0-255], preserveLum bool,
// blendMode: "normal" | "screen" | "multiply" | "overlay".
CG.photoFilter = function (img, density, rgb, preserveLum, blendMode) {
    var out = CG.cloneImage(img);
    var d = out.data;
    var len = Buffer.size(d);
    var mixRatio = density / 100;
    for (var i = 0; i < len; i += 4) {
        var r = d[i];
        var g = d[i + 1];
        var b = d[i + 2];
        var ol = 0;
        if (preserveLum) {
            ol = (Math.max(r, Math.max(g, b)) + Math.min(r, Math.min(g, b))) / 2 / 255;
        }
        if (blendMode === "multiply") {
            r = ((rgb[0] / 255 * r / 255) * 255 * mixRatio) + (r * (1 - mixRatio));
            g = ((rgb[1] / 255 * g / 255) * 255 * mixRatio) + (g * (1 - mixRatio));
            b = ((rgb[2] / 255 * b / 255) * 255 * mixRatio) + (b * (1 - mixRatio));
        } else if (blendMode === "overlay") {
            var rt = r / 255;
            var gt = g / 255;
            var bt = b / 255;
            var br = rgb[0] / 255;
            var bg = rgb[1] / 255;
            var bb = rgb[2] / 255;
            if (rt > 0.5) rt = (1 - (1 - 2 * (rt - 0.5)) * (1 - br));
            else rt = (2 * rt) * br;
            if (gt > 0.5) gt = (1 - (1 - 2 * (gt - 0.5)) * (1 - bg));
            else gt = (2 * gt) * bg;
            if (bt > 0.5) bt = (1 - (1 - 2 * (bt - 0.5)) * (1 - bb));
            else bt = (2 * bt) * bb;
            r = ((1 - mixRatio) * r) + (mixRatio * (rt * 255));
            g = ((1 - mixRatio) * g) + (mixRatio * (gt * 255));
            b = ((1 - mixRatio) * b) + (mixRatio * (bt * 255));
        } else if (blendMode === "screen") {
            r = ((1 - mixRatio) * r) + (mixRatio * (255 - (255 - r) * (255 - rgb[0]) / 255));
            g = ((1 - mixRatio) * g) + (mixRatio * (255 - (255 - g) * (255 - rgb[1]) / 255));
            b = ((1 - mixRatio) * b) + (mixRatio * (255 - (255 - b) * (255 - rgb[2]) / 255));
        } else {
            r = ((1 - mixRatio) * r) + (mixRatio * rgb[0]);
            g = ((1 - mixRatio) * g) + (mixRatio * rgb[1]);
            b = ((1 - mixRatio) * b) + (mixRatio * rgb[2]);
        }
        if (preserveLum) {
            var hsl = CG._tRGBToHSL(r, g, b);
            var rr = CG._tHSLToRGB(hsl.h, hsl.s, ol);
            d[i] = rr.r;
            d[i + 1] = rr.g;
            d[i + 2] = rr.b;
        } else {
            d[i] = CG._clamp255(r);
            d[i + 1] = CG._clamp255(g);
            d[i + 2] = CG._clamp255(b);
        }
    }
    return out;
};

// Channel mixer. params structure:
// { red:{red_gain,green_gain,blue_gain}, green:{...}, blue:{...}, black:{...},
//   monochrome:bool, preserve_luminosity:bool }
// Gains are unitless floats, typical range [-2.0, 2.0], applied as linear weights.
CG.channelMixer = function (img, params) {
    var out = CG.cloneImage(img);
    var d = out.data;
    var len = Buffer.size(d);
    var sum;
    var redNorm = 1.0;
    var greenNorm = 1.0;
    var blueNorm = 1.0;
    var blackNorm = 1.0;
    if (params.preserve_luminosity) {
        sum = params.red.red_gain + params.red.green_gain + params.red.blue_gain;
        if (sum !== 0.0) redNorm = Math.abs(1 / sum);
        sum = params.green.red_gain + params.green.green_gain + params.green.blue_gain;
        if (sum !== 0.0) greenNorm = Math.abs(1 / sum);
        sum = params.blue.red_gain + params.blue.green_gain + params.blue.blue_gain;
        if (sum !== 0.0) blueNorm = Math.abs(1 / sum);
        sum = params.black.red_gain + params.black.green_gain + params.black.blue_gain;
        if (sum !== 0.0) blackNorm = Math.abs(1 / sum);
    }
    for (var i = 0; i < len; i += 4) {
        var r = d[i];
        var g = d[i + 1];
        var b = d[i + 2];
        if (params.monochrome) {
            var c = params.black.red_gain * r + params.black.green_gain * g + params.black.blue_gain * b;
            c = CG._clamp255(c * blackNorm);
            d[i] = c;
            d[i + 1] = c;
            d[i + 2] = c;
        } else {
            d[i] = CG._clamp255((params.red.red_gain * r + params.red.green_gain * g + params.red.blue_gain * b) * redNorm);
            d[i + 1] = CG._clamp255((params.green.red_gain * r + params.green.green_gain * g + params.green.blue_gain * b) * greenNorm);
            d[i + 2] = CG._clamp255((params.blue.red_gain * r + params.blue.green_gain * g + params.blue.blue_gain * b) * blueNorm);
        }
    }
    return out;
};

// Color balance with shadows/midtones/highlights.
// params structure:
// { shadows:{cyan_red,magenta_green,yellow_blue},
//   midtones:{cyan_red,magenta_green,yellow_blue},
//   highlights:{cyan_red,magenta_green,yellow_blue},
//   preserve_luminosity:bool }
// Adjustments are unitless floats, typical range [-100, 100] (like Photoshop sliders).
CG.colorBalance = function (img, params) {
    var out = CG.cloneImage(img);
    var d = out.data;
    var len = Buffer.size(d);
    var CLAMP = function (a, b, c) {
        if (a < b) a = b;
        if (a > c) a = c;
        return a;
    };
    var CLAMP0255 = function (a) {
        return CLAMP(a, 0, 255);
    };
    var rgb_to_hsl_int = function (t) {
        var u = CG._tRGBToHSL(t.r_n, t.g_n, t.b_n);
        t.r_n = u.h;
        t.g_n = u.s;
        t.b_n = u.l;
    };
    var rgb_to_l_int = function (p) {
        var u = CG._tRGBToHSL(p.r, p.g, p.b);
        return u.l;
    };
    var hsl_to_rgb_int = function (t) {
        var u = CG._tHSLToRGB(t.r_n, t.g_n, t.b_n);
        t.r_n = u.r;
        t.g_n = u.g;
        t.b_n = u.b;
    };
    var highlights = [];
    var midtones = [];
    var shadows = [];
    for (var i = 0; i < 256; i++) {
        var a = 64;
        var b = 85;
        var scale = 1.785;
        var low = CLAMP((i - b) / -a + 0.5, 0, 1) * scale;
        var mid = CLAMP((i - b) / a + 0.5, 0, 1) * CLAMP((i + b - 255) / -a + 0.5, 0, 1) * scale;
        shadows[i] = low;
        midtones[i] = mid;
        highlights[255 - i] = low;
    }
    var rlut = [];
    var glut = [];
    var blut = [];
    var temp = { r_n: 0, g_n: 0, b_n: 0 };
    for (var i = 0; i < 256; i++) {
        temp.r_n = i;
        temp.g_n = i;
        temp.b_n = i;
        temp.r_n += params.shadows.cyan_red * shadows[i];
        temp.r_n += params.midtones.cyan_red * midtones[i];
        temp.r_n += params.highlights.cyan_red * highlights[i];
        temp.r_n = CLAMP0255(temp.r_n);
        temp.g_n += params.shadows.magenta_green * shadows[i];
        temp.g_n += params.midtones.magenta_green * midtones[i];
        temp.g_n += params.highlights.magenta_green * highlights[i];
        temp.g_n = CLAMP0255(temp.g_n);
        temp.b_n += params.shadows.yellow_blue * shadows[i];
        temp.b_n += params.midtones.yellow_blue * midtones[i];
        temp.b_n += params.highlights.yellow_blue * highlights[i];
        temp.b_n = CLAMP0255(temp.b_n);
        rlut[i] = temp.r_n;
        glut[i] = temp.g_n;
        blut[i] = temp.b_n;
    }
    var pix = { r: 0, g: 0, b: 0 };
    for (var i = 0; i < len; i += 4) {
        pix.r = d[i];
        pix.g = d[i + 1];
        pix.b = d[i + 2];
        temp.r_n = rlut[pix.r];
        temp.g_n = glut[pix.g];
        temp.b_n = blut[pix.b];
        if (params.preserve_luminosity) {
            rgb_to_hsl_int(temp);
            temp.b_n = rgb_to_l_int(pix);
            hsl_to_rgb_int(temp);
        }
        d[i] = temp.r_n;
        d[i + 1] = temp.g_n;
        d[i + 2] = temp.b_n;
    }
    return out;
};

// Flip image horizontally.
CG.flipHorizontal = function (img) {
    var out = CG.createImage(img.width, img.height);
    var w = img.width;
    var h = img.height;
    var src = img.data;
    var dst = out.data;
    for (var y = 0; y < h; y++) {
        for (var x = 0; x < w; x++) {
            var off = (y * w + x) * 4;
            var dstOff = (y * w + (w - x - 1)) * 4;
            dst[dstOff] = src[off];
            dst[dstOff + 1] = src[off + 1];
            dst[dstOff + 2] = src[off + 2];
            dst[dstOff + 3] = src[off + 3];
        }
    }
    return out;
};

// Flip image vertically.
CG.flipVertical = function (img) {
    var out = CG.createImage(img.width, img.height);
    var w = img.width;
    var h = img.height;
    var src = img.data;
    var dst = out.data;
    for (var y = 0; y < h; y++) {
        for (var x = 0; x < w; x++) {
            var off = (y * w + x) * 4;
            var dstOff = ((h - y - 1) * w + x) * 4;
            dst[dstOff] = src[off];
            dst[dstOff + 1] = src[off + 1];
            dst[dstOff + 2] = src[off + 2];
            dst[dstOff + 3] = src[off + 3];
        }
    }
    return out;
};

// Rotate 90 degrees clockwise.
CG.rotate90 = function (img) {
    var out = CG.createImage(img.height, img.width);
    var src = img.data;
    var dst = out.data;
    var sw = img.width;
    var sh = img.height;
    for (var y = 0; y < sh; y++) {
        for (var x = 0; x < sw; x++) {
            var srcOff = (y * sw + x) * 4;
            var dx = sh - y - 1;
            var dy = x;
            var dstOff = (dy * out.width + dx) * 4;
            dst[dstOff] = src[srcOff];
            dst[dstOff + 1] = src[srcOff + 1];
            dst[dstOff + 2] = src[srcOff + 2];
            dst[dstOff + 3] = src[srcOff + 3];
        }
    }
    return out;
};

// Rotate 180 degrees.
CG.rotate180 = function (img) {
    var out = CG.createImage(img.width, img.height);
    var src = img.data;
    var dst = out.data;
    var w = img.width;
    var h = img.height;
    for (var y = 0; y < h; y++) {
        for (var x = 0; x < w; x++) {
            var srcOff = (y * w + x) * 4;
            var dx = w - x - 1;
            var dy = h - y - 1;
            var dstOff = (dy * w + dx) * 4;
            dst[dstOff] = src[srcOff];
            dst[dstOff + 1] = src[srcOff + 1];
            dst[dstOff + 2] = src[srcOff + 2];
            dst[dstOff + 3] = src[srcOff + 3];
        }
    }
    return out;
};

// Rotate 270 degrees clockwise (90 CCW).
CG.rotate270 = function (img) {
    var out = CG.createImage(img.height, img.width);
    var src = img.data;
    var dst = out.data;
    var sw = img.width;
    var sh = img.height;
    for (var y = 0; y < sh; y++) {
        for (var x = 0; x < sw; x++) {
            var srcOff = (y * sw + x) * 4;
            var dx = y;
            var dy = sw - x - 1;
            var dstOff = (dy * out.width + dx) * 4;
            dst[dstOff] = src[srcOff];
            dst[dstOff + 1] = src[srcOff + 1];
            dst[dstOff + 2] = src[srcOff + 2];
            dst[dstOff + 3] = src[srcOff + 3];
        }
    }
    return out;
};

// Rotate arbitrary angle in degrees. resize true expands bounds.
CG.rotate = function (img, angleDegrees, resize) {
    var rad = angleDegrees * Math.PI / 180;
    var cosA = Math.cos(rad);
    var sinA = Math.sin(rad);
    var sw = img.width;
    var sh = img.height;
    var cx = sw / 2;
    var cy = sh / 2;
    var outW = sw;
    var outH = sh;
    if (resize) {
        var corners = [
            [-cx, -cy],
            [sw - cx, -cy],
            [sw - cx, sh - cy],
            [-cx, sh - cy]
        ];
        var minX = 0, minY = 0, maxX = 0, maxY = 0;
        for (var i = 0; i < corners.length; i++) {
            var x = corners[i][0];
            var y = corners[i][1];
            var rx = x * cosA - y * sinA;
            var ry = x * sinA + y * cosA;
            if (i === 0 || rx < minX) minX = rx;
            if (i === 0 || ry < minY) minY = ry;
            if (i === 0 || rx > maxX) maxX = rx;
            if (i === 0 || ry > maxY) maxY = ry;
        }
        outW = Math.ceil(maxX - minX);
        outH = Math.ceil(maxY - minY);
    }
    var out = CG.createImage(outW, outH);
    var dst = out.data;
    var src = img.data;
    var dx = outW / 2;
    var dy = outH / 2;
    for (var y = 0; y < outH; y++) {
        for (var x = 0; x < outW; x++) {
            var tx = x - dx;
            var ty = y - dy;
            var sx = tx * cosA + ty * sinA + cx;
            var sy = -tx * sinA + ty * cosA + cy;
            var dstIndex = (y * outW + x) * 4;
            CG._sampleBilinear(src, sw, sh, sx, sy, dst, dstIndex);
        }
    }
    return out;
};

// Crop rectangle. x/y/width/height in pixels.
CG.crop = function (img, x, y, width, height) {
    var out = CG.createImage(width, height);
    var src = img.data;
    var dst = out.data;
    for (var j = 0; j < height; j++) {
        for (var i = 0; i < width; i++) {
            var srcIndex = CG._getIndexClamp(x + i, y + j, img.width, img.height);
            var dstIndex = (j * width + i) * 4;
            dst[dstIndex] = src[srcIndex];
            dst[dstIndex + 1] = src[srcIndex + 1];
            dst[dstIndex + 2] = src[srcIndex + 2];
            dst[dstIndex + 3] = src[srcIndex + 3];
        }
    }
    return out;
};

// Resize nearest neighbor. width/height in pixels.
CG.resizeNearest = function (img, width, height) {
    var out = CG.createImage(width, height);
    var src = img.data;
    var dst = out.data;
    var sw = img.width;
    var sh = img.height;
    for (var y = 0; y < height; y++) {
        var sy = Math.floor(y * sh / height);
        for (var x = 0; x < width; x++) {
            var sx = Math.floor(x * sw / width);
            var srcIndex = (sy * sw + sx) * 4;
            var dstIndex = (y * width + x) * 4;
            dst[dstIndex] = src[srcIndex];
            dst[dstIndex + 1] = src[srcIndex + 1];
            dst[dstIndex + 2] = src[srcIndex + 2];
            dst[dstIndex + 3] = src[srcIndex + 3];
        }
    }
    return out;
};

// Resize bilinear. width/height in pixels.
CG.resizeBilinear = function (img, width, height) {
    var out = CG.createImage(width, height);
    var src = img.data;
    var dst = out.data;
    var sw = img.width;
    var sh = img.height;
    for (var y = 0; y < height; y++) {
        var sy = (y + 0.5) * (sh / height) - 0.5;
        for (var x = 0; x < width; x++) {
            var sx = (x + 0.5) * (sw / width) - 0.5;
            var dstIndex = (y * width + x) * 4;
            CG._sampleBilinear(src, sw, sh, sx, sy, dst, dstIndex);
        }
    }
    return out;
};

// Uniform rescale. scale is float multiplier (e.g. 0.5, 2.0).
CG.rescale = function (img, scale) {
    var w = Math.max(1, Math.round(img.width * scale));
    var h = Math.max(1, Math.round(img.height * scale));
    return CG.resizeBilinear(img, w, h);
};

// Convolve with square kernel. weights length must be square.
CG._convolve = function (img, weights, opaque) {
    var side = Math.round(Math.sqrt(weights.length));
    var halfSide = Math.floor(side / 2);
    var src = img.data;
    var sw = img.width;
    var sh = img.height;
    var out = CG.createImage(sw, sh);
    var dst = out.data;
    var alphaFac = opaque ? 1 : 0;
    for (var y = 0; y < sh; y++) {
        for (var x = 0; x < sw; x++) {
            var dstOff = (y * sw + x) * 4;
            var r = 0, g = 0, b = 0, a = 0;
            for (var cy = 0; cy < side; cy++) {
                for (var cx = 0; cx < side; cx++) {
                    var scy = Math.min(sh - 1, Math.max(0, y + cy - halfSide));
                    var scx = Math.min(sw - 1, Math.max(0, x + cx - halfSide));
                    var srcOff = (scy * sw + scx) * 4;
                    var wt = weights[cy * side + cx];
                    r += src[srcOff] * wt;
                    g += src[srcOff + 1] * wt;
                    b += src[srcOff + 2] * wt;
                    a += src[srcOff + 3] * wt;
                }
            }
            dst[dstOff] = CG._clamp255(r);
            dst[dstOff + 1] = CG._clamp255(g);
            dst[dstOff + 2] = CG._clamp255(b);
            dst[dstOff + 3] = CG._clamp255(a + alphaFac * (255 - a));
        }
    }
    return out;
};

// Gaussian blur. radius in pixels, integer >= 0.
CG.gaussianBlur = function (img, radius) {
    radius = Math.floor(radius);
    if (radius <= 0) return CG.cloneImage(img);
    var sigma = radius / 3;
    var size = radius * 2 + 1;
    var weights = [];
    var sum = 0;
    for (var i = -radius; i <= radius; i++) {
        var w = Math.exp(-(i * i) / (2 * sigma * sigma));
        weights.push(w);
        sum += w;
    }
    for (var i = 0; i < size; i++) weights[i] /= sum;
    var sw = img.width;
    var sh = img.height;
    var src = img.data;
    var tmp = CG.createImage(sw, sh);
    var dst = tmp.data;
    for (var y = 0; y < sh; y++) {
        for (var x = 0; x < sw; x++) {
            var r = 0, g = 0, b = 0, a = 0;
            for (var k = -radius; k <= radius; k++) {
                var sx = Math.min(sw - 1, Math.max(0, x + k));
                var srcOff = (y * sw + sx) * 4;
                var wv = weights[k + radius];
                r += src[srcOff] * wv;
                g += src[srcOff + 1] * wv;
                b += src[srcOff + 2] * wv;
                a += src[srcOff + 3] * wv;
            }
            var dstOff = (y * sw + x) * 4;
            dst[dstOff] = r;
            dst[dstOff + 1] = g;
            dst[dstOff + 2] = b;
            dst[dstOff + 3] = a;
        }
    }
    var out = CG.createImage(sw, sh);
    var outd = out.data;
    for (var y = 0; y < sh; y++) {
        for (var x = 0; x < sw; x++) {
            var r = 0, g = 0, b = 0, a = 0;
            for (var k = -radius; k <= radius; k++) {
                var sy = Math.min(sh - 1, Math.max(0, y + k));
                var srcOff = (sy * sw + x) * 4;
                var wv = weights[k + radius];
                r += dst[srcOff] * wv;
                g += dst[srcOff + 1] * wv;
                b += dst[srcOff + 2] * wv;
                a += dst[srcOff + 3] * wv;
            }
            var dstOff = (y * sw + x) * 4;
            outd[dstOff] = CG._clamp255(r);
            outd[dstOff + 1] = CG._clamp255(g);
            outd[dstOff + 2] = CG._clamp255(b);
            outd[dstOff + 3] = CG._clamp255(a);
        }
    }
    return out;
};

// Box blur. radius in pixels, integer >= 0.
CG.boxBlur = function (img, radius) {
    radius = Math.floor(radius);
    if (radius <= 0) return CG.cloneImage(img);
    var size = radius * 2 + 1;
    var weights = [];
    for (var i = 0; i < size * size; i++) weights[i] = 1 / (size * size);
    return CG._convolve(img, weights, true);
};

// Blur (box blur radius 1).
CG.blur = function (img) {
    return CG.boxBlur(img, 1);
};

// Blur more (box blur radius 2).
CG.blurMore = function (img) {
    return CG.boxBlur(img, 2);
};

// Sharpen using 3x3 kernel.
CG.sharpen = function (img) {
    return CG._convolve(img, [0, -1, 0, -1, 5, -1, 0, -1, 0], true);
};

// Stronger sharpen using 3x3 kernel.
CG.sharpenMore = function (img) {
    return CG._convolve(img, [-1, -1, -1, -1, 9, -1, -1, -1, -1], true);
};

// Unsharp mask. radius in pixels, amount float (typical 0-2).
CG.unsharpMask = function (img, radius, amount) {
    var blurred = CG.gaussianBlur(img, radius);
    var out = CG.cloneImage(img);
    var src = img.data;
    var blr = blurred.data;
    var d = out.data;
    var len = Buffer.size(d);
    for (var i = 0; i < len; i += 4) {
        d[i] = CG._clamp255(src[i] + (src[i] - blr[i]) * amount);
        d[i + 1] = CG._clamp255(src[i + 1] + (src[i + 1] - blr[i + 1]) * amount);
        d[i + 2] = CG._clamp255(src[i + 2] + (src[i + 2] - blr[i + 2]) * amount);
    }
    return out;
};

// Sobel edge detect (grayscale output).
CG.sobel = function (img) {
    var gray = CG.desaturate(img);
    var w = img.width;
    var h = img.height;
    var src = gray.data;
    var out = CG.createImage(w, h);
    var d = out.data;
    var kernelX = [-1, 0, 1, -2, 0, 2, -1, 0, 1];
    var kernelY = [-1, -2, -1, 0, 0, 0, 1, 2, 1];
    for (var y = 0; y < h; y++) {
        for (var x = 0; x < w; x++) {
            var gx = 0;
            var gy = 0;
            for (var ky = -1; ky <= 1; ky++) {
                for (var kx = -1; kx <= 1; kx++) {
                    var idx = CG._getIndexClamp(x + kx, y + ky, w, h);
                    var val = src[idx];
                    var k = (ky + 1) * 3 + (kx + 1);
                    gx += val * kernelX[k];
                    gy += val * kernelY[k];
                }
            }
            var v = Math.sqrt(gx * gx + gy * gy);
            var off = (y * w + x) * 4;
            v = CG._clamp255(v);
            d[off] = v;
            d[off + 1] = v;
            d[off + 2] = v;
            d[off + 3] = 255;
        }
    }
    return out;
};

// Alias for sobel().
CG.edgeDetect = CG.sobel;

// Emboss filter using 3x3 kernel.
CG.emboss = function (img) {
    return CG._convolve(img, [-2, -1, 0, -1, 1, 1, 0, 1, 2], true);
};

// Solarize above threshold. threshold in [0,255].
CG.solarize = function (img, threshold) {
    var out = CG.cloneImage(img);
    var d = out.data;
    var len = Buffer.size(d);
    for (var i = 0; i < len; i += 4) {
        if (d[i] > threshold) d[i] = 255 - d[i];
        if (d[i + 1] > threshold) d[i + 1] = 255 - d[i + 1];
        if (d[i + 2] > threshold) d[i + 2] = 255 - d[i + 2];
    }
    return out;
};

// Sepia tone. No params.
CG.sepia = function (img) {
    var out = CG.cloneImage(img);
    var d = out.data;
    var len = Buffer.size(d);
    for (var i = 0; i < len; i += 4) {
        var r = d[i];
        var g = d[i + 1];
        var b = d[i + 2];
        d[i] = CG._clamp255(0.393 * r + 0.769 * g + 0.189 * b);
        d[i + 1] = CG._clamp255(0.349 * r + 0.686 * g + 0.168 * b);
        d[i + 2] = CG._clamp255(0.272 * r + 0.534 * g + 0.131 * b);
    }
    return out;
};

// Mosaic pixelate. blockSize in pixels, integer >= 1.
CG.mosaic = function (img, blockSize) {
    var out = CG.cloneImage(img);
    var d = out.data;
    var w = img.width;
    var h = img.height;
    for (var y = 0; y < h; y += blockSize) {
        for (var x = 0; x < w; x += blockSize) {
            var idx = (y * w + x) * 4;
            var r = d[idx];
            var g = d[idx + 1];
            var b = d[idx + 2];
            var a = d[idx + 3];
            var maxY = Math.min(h, y + blockSize);
            var maxX = Math.min(w, x + blockSize);
            for (var yy = y; yy < maxY; yy++) {
                for (var xx = x; xx < maxX; xx++) {
                    var off = (yy * w + xx) * 4;
                    d[off] = r;
                    d[off + 1] = g;
                    d[off + 2] = b;
                    d[off + 3] = a;
                }
            }
        }
    }
    return out;
};

// Sine distortion. amount/yamount are pixel offsets.
CG.distortSine = function (img, amount, yamount) {
    var out = CG.createImage(img.width, img.height);
    var w = img.width;
    var h = img.height;
    var src = img.data;
    var dst = out.data;
    for (var y = 0; y < h; y++) {
        for (var x = 0; x < w; x++) {
            var sx = x + Math.sin(y / h * Math.PI * 2) * amount;
            var sy = y + Math.sin(x / w * Math.PI * 2) * yamount;
            var dstIndex = (y * w + x) * 4;
            CG._sampleBilinear(src, w, h, sx, sy, dst, dstIndex);
        }
    }
    return out;
};

// Twirl around center. centerX/Y in pixels, radius in pixels, angle in degrees.
CG.twirl = function (img, centerX, centerY, radius, angle) {
    var out = CG.createImage(img.width, img.height);
    var w = img.width;
    var h = img.height;
    var src = img.data;
    var dst = out.data;
    var angleRad = angle * Math.PI / 180;
    for (var y = 0; y < h; y++) {
        for (var x = 0; x < w; x++) {
            var dx = x - centerX;
            var dy = y - centerY;
            var dist = Math.sqrt(dx * dx + dy * dy);
            var dstIndex = (y * w + x) * 4;
            if (dist < radius) {
                var a = Math.atan2(dy, dx) + angleRad * (radius - dist) / radius;
                var sx = centerX + dist * Math.cos(a);
                var sy = centerY + dist * Math.sin(a);
                CG._sampleBilinear(src, w, h, sx, sy, dst, dstIndex);
            } else {
                var srcIndex = (y * w + x) * 4;
                dst[dstIndex] = src[srcIndex];
                dst[dstIndex + 1] = src[srcIndex + 1];
                dst[dstIndex + 2] = src[srcIndex + 2];
                dst[dstIndex + 3] = src[srcIndex + 3];
            }
        }
    }
    return out;
};

// Median filter. radius in pixels, integer >= 1.
CG.median = function (img, radius) {
    radius = Math.floor(radius);
    if (radius <= 0) return CG.cloneImage(img);
    var w = img.width;
    var h = img.height;
    var out = CG.createImage(w, h);
    var src = img.data;
    var dst = out.data;
    var size = (radius * 2 + 1) * (radius * 2 + 1);
    var windowR = [];
    var windowG = [];
    var windowB = [];
    for (var y = 0; y < h; y++) {
        for (var x = 0; x < w; x++) {
            var n = 0;
            for (var ky = -radius; ky <= radius; ky++) {
                for (var kx = -radius; kx <= radius; kx++) {
                    var idx = CG._getIndexClamp(x + kx, y + ky, w, h);
                    windowR[n] = src[idx];
                    windowG[n] = src[idx + 1];
                    windowB[n] = src[idx + 2];
                    n++;
                }
            }
            windowR.sort(function (a, b) { return a - b; });
            windowG.sort(function (a, b) { return a - b; });
            windowB.sort(function (a, b) { return a - b; });
            var mid = (size / 2) | 0;
            var off = (y * w + x) * 4;
            dst[off] = windowR[mid];
            dst[off + 1] = windowG[mid];
            dst[off + 2] = windowB[mid];
            dst[off + 3] = src[off + 3];
        }
    }
    return out;
};

// Despeckle (median radius 1).
CG.despeckle = function (img) {
    return CG.median(img, 1);
};

// Add noise. amount in [0,255] (amplitude).
CG.addNoise = function (img, amount) {
    var out = CG.cloneImage(img);
    var d = out.data;
    var len = Buffer.size(d);
    for (var i = 0; i < len; i += 4) {
        var n = (Math.random() - 0.5) * amount;
        d[i] = CG._clamp255(d[i] + n);
        d[i + 1] = CG._clamp255(d[i + 1] + n);
        d[i + 2] = CG._clamp255(d[i + 2] + n);
    }
    return out;
};

// Bilateral filter. sigmap/sigmaf in pixels, size is kernel size (odd int).
CG.bilateral = function (img, sigmap, sigmaf, size) {
    var w = img.width;
    var h = img.height;
    var out = CG.createImage(w, h);
    var src = img.data;
    var dst = out.data;
    var wf = Math.floor(size / 2);
    var hf = Math.floor(size / 2);
    var sigmaf2 = sigmaf * sigmaf * 2;
    var fp = [];
    var sum = 0;
    for (var y = -hf; y <= hf; y++) {
        for (var x = -wf; x <= wf; x++) {
            var v = Math.exp(-(x * x + y * y) / (2 * sigmap * sigmap));
            fp.push(v);
            sum += v;
        }
    }
    for (var i = 0; i < fp.length; i++) fp[i] /= sum;
    for (var y = 0; y < h; y++) {
        for (var x = 0; x < w; x++) {
            var idx = (y * w + x) * 4;
            var r0 = src[idx];
            var g0 = src[idx + 1];
            var b0 = src[idx + 2];
            var r = 0, g = 0, b = 0;
            var wsum = 0;
            var fidx = 0;
            for (var ky = -hf; ky <= hf; ky++) {
                for (var kx = -wf; kx <= wf; kx++) {
                    var px = Math.min(w - 1, Math.max(0, x + kx));
                    var py = Math.min(h - 1, Math.max(0, y + ky));
                    var pidx = (py * w + px) * 4;
                    var weight = fp[fidx++];
                    var dr = src[pidx] - r0;
                    var dg = src[pidx + 1] - g0;
                    var db = src[pidx + 2] - b0;
                    weight *= Math.exp(-(dr * dr + dg * dg + db * db) / sigmaf2);
                    wsum += weight;
                    r += src[pidx] * weight;
                    g += src[pidx + 1] * weight;
                    b += src[pidx + 2] * weight;
                }
            }
            dst[idx] = CG._clamp255(r / wsum);
            dst[idx + 1] = CG._clamp255(g / wsum);
            dst[idx + 2] = CG._clamp255(b / wsum);
            dst[idx + 3] = src[idx + 3];
        }
    }
    return out;
};

// Motion blur. size in pixels, angle in degrees.
CG.motionBlur = function (img, size, angle) {
    size = Math.max(1, Math.floor(size));
    var rad = angle * Math.PI / 180;
    var cosA = Math.cos(rad);
    var sinA = Math.sin(rad);
    var kernel = [];
    for (var i = 0; i < size * size; i++) kernel[i] = 0;
    var half = Math.floor(size / 2);
    var sum = 0;
    for (var i = -half; i <= half; i++) {
        var x = Math.round(half + i * cosA);
        var y = Math.round(half + i * sinA);
        if (x >= 0 && x < size && y >= 0 && y < size) {
            kernel[y * size + x] = 1;
            sum += 1;
        }
    }
    for (var i = 0; i < kernel.length; i++) kernel[i] /= sum;
    return CG._convolve(img, kernel, true);
};

// Difference of Gaussians. radii in pixels; normalize/invert booleans.
CG.diffGauss = function (img, radius1, radius2, normalize, invert) {
    var blur1 = CG.gaussianBlur(img, radius1);
    var blur2 = CG.gaussianBlur(img, radius2);
    var out = CG.createImage(img.width, img.height);
    var d = out.data;
    var a = blur1.data;
    var b = blur2.data;
    var len = Buffer.size(d);
    var maxv = 0;
    for (var i = 0; i < len; i += 4) {
        var v = a[i] - b[i];
        if (v > maxv) maxv = v;
    }
    for (var i = 0; i < len; i += 4) {
        var v = a[i] - b[i];
        if (normalize && maxv !== 0) v = v * (255 / maxv);
        if (invert) v = 255 - v;
        v = CG._clamp255(v);
        d[i] = v;
        d[i + 1] = v;
        d[i + 2] = v;
        d[i + 3] = 255;
    }
    return out;
};

// Erosion (min filter). size is kernel size in pixels (odd int).
CG.erosion = function (img, size) {
    size = Math.max(1, Math.floor(size));
    var radius = Math.floor(size / 2);
    var w = img.width;
    var h = img.height;
    var out = CG.createImage(w, h);
    var src = img.data;
    var dst = out.data;
    for (var y = 0; y < h; y++) {
        for (var x = 0; x < w; x++) {
            var minR = 255, minG = 255, minB = 255;
            for (var ky = -radius; ky <= radius; ky++) {
                for (var kx = -radius; kx <= radius; kx++) {
                    var idx = CG._getIndexClamp(x + kx, y + ky, w, h);
                    if (src[idx] < minR) minR = src[idx];
                    if (src[idx + 1] < minG) minG = src[idx + 1];
                    if (src[idx + 2] < minB) minB = src[idx + 2];
                }
            }
            var off = (y * w + x) * 4;
            dst[off] = minR;
            dst[off + 1] = minG;
            dst[off + 2] = minB;
            dst[off + 3] = src[off + 3];
        }
    }
    return out;
};

// Dilation (max filter). size is kernel size in pixels (odd int).
CG.dilation = function (img, size) {
    size = Math.max(1, Math.floor(size));
    var radius = Math.floor(size / 2);
    var w = img.width;
    var h = img.height;
    var out = CG.createImage(w, h);
    var src = img.data;
    var dst = out.data;
    for (var y = 0; y < h; y++) {
        for (var x = 0; x < w; x++) {
            var maxR = 0, maxG = 0, maxB = 0;
            for (var ky = -radius; ky <= radius; ky++) {
                for (var kx = -radius; kx <= radius; kx++) {
                    var idx = CG._getIndexClamp(x + kx, y + ky, w, h);
                    if (src[idx] > maxR) maxR = src[idx];
                    if (src[idx + 1] > maxG) maxG = src[idx + 1];
                    if (src[idx + 2] > maxB) maxB = src[idx + 2];
                }
            }
            var off = (y * w + x) * 4;
            dst[off] = maxR;
            dst[off + 1] = maxG;
            dst[off + 2] = maxB;
            dst[off + 3] = src[off + 3];
        }
    }
    return out;
};

// Legacy indexed color (posterize + optional FS dither). levels in [2,16], dither boolean.
CG._indexedColorLegacy = function (img, levels, dither) {
    var out = CG.cloneImage(img);
    var d = out.data;
    var len = Buffer.size(d);
    levels = Math.max(2, Math.min(16, levels));
    var step = 255 / (levels - 1);
    if (dither) {
        return CG.dither(out, levels);
    }
    for (var i = 0; i < len; i += 4) {
        d[i] = Math.round(d[i] / step) * step;
        d[i + 1] = Math.round(d[i + 1] / step) * step;
        d[i + 2] = Math.round(d[i + 2] / step) * step;
    }
    return out;
};

// Indexed color via RgbQuant. colors in [2,256], preset in {"ada","exa","uni","mac","win","web"}.
// dithKern in CG.rgbQuantDitherKernels, dithSerp boolean for serpentine diffusion.
// sampleMax caps the sampling image size (0 = no cap).
// workMax caps the working image size before resampling back (0 = no cap).
CG.indexedColor = function (img, colors, preset, dithKern, dithSerp, sampleMax, workMax) {
    if (typeof preset == "boolean") {
        return CG._indexedColorLegacy(img, colors, preset);
    }
    if (!preset) preset = "ada";
    return CG.rgbQuantizePreset(img, preset, colors, dithKern, dithSerp, sampleMax, workMax);
};

// Bitmap (1-bit) via a strict 2-color palette (black/white) and optional dither.
CG.bitmap = function (img, dithKern, dithSerp) {
    var kernel = (typeof dithKern == "string") ? dithKern : "FloydSteinberg";
    return CG.rgbQuantizePreset(img, "bitmap", 2, kernel, dithSerp);
};
