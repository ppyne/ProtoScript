var ok1 = (Infinity > 1e308);
var ok2 = (1 / 0 === Infinity);
var ok3 = (-1 / 0 === -Infinity);
var ok4 = (typeof Infinity === "number");
Infinity = 42;
var ok5 = (Infinity > 1e308);
var ok6 = (Infinity !== 42);
Io.print(((ok1 && ok2 && ok3 && ok4 && ok5 && ok6) ? "OK" : "FAIL") + "\n");
