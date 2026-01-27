var col = 4284250757;
var r = col & 0xff;
var g = (col >> 8) & 0xff;
var b = (col >> 16) & 0xff;
var a = (col >>> 24) & 0xff;
Io.print("r=" + r + " g=" + g + " b=" + b + " a=" + a + "\n");
