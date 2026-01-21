var ok1 = (NaN !== NaN);
NaN = 1;
var ok2 = (NaN !== NaN);
Io.print((ok1 && ok2 ? "OK" : "FAIL") + "\n");
