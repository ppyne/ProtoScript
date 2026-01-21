var pi0 = Math.PI;
Math.PI = 4;
Io.print((Math.PI == pi0 )+ "\n");
var del = delete Math.PI;
Io.print((Math.PI == pi0 )+ "\n");
Io.print((del )+ "\n");

var abs0 = Math.abs(-2);
delete Math.abs;
Io.print((Math.abs(-2) == abs0 )+ "\n");
