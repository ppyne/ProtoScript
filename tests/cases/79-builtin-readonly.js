var before = Object.prototype.toString;
Object.prototype.toString = 1;
Io.print(Object.prototype.toString === before);
var del = delete Object.prototype.toString;
Io.print(del);
Io.print(Object.prototype.toString === before);
