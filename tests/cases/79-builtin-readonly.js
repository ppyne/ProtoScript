var before = Object.prototype.toString;
Object.prototype.toString = 1;
Io.print((Object.prototype.toString === before )+ "\n");
var del = delete Object.prototype.toString;
Io.print((del )+ "\n");
Io.print((Object.prototype.toString === before )+ "\n");
