include "examples/computer_graphics/rgbquant.js";
var rq = new RgbQuant({ colors: 16, method: 2, boxSize: [40,40], boxPxls: 3 });
Io.print("sampleBits=" + rq.sampleBits + "\n");
