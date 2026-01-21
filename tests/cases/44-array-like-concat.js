var a = new Array(1);
var o = new Object();
o[0] = "x";
o[1] = "y";
o.length = 2;
var c = a.concat(o, "z");
Io.print(c.toString());
