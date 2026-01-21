try {
  Function.prototype.call.call(1);
} catch (e) {
  Io.print((e.name )+ "\n");
}

try {
  var re = new Object();
  RegExp.prototype.exec.call(re, "a");
} catch (e) {
  Io.print((e.name )+ "\n");
}

try {
  var d = new Object();
  Date.prototype.getTime.call(d);
} catch (e) {
  Io.print((e.name )+ "\n");
}

try {
  var o = new Object();
  o.toString = 1;
  Object.prototype.toLocaleString.call(o);
} catch (e) {
  Io.print((e.name )+ "\n");
}

try {
  Object.prototype.hasOwnProperty.call(null, "x");
} catch (e) {
  Io.print((e.name )+ "\n");
}
