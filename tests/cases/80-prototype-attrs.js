function add(a,b){ return a+b; }
Io.print((Object.prototype.propertyIsEnumerable.call(add, "prototype") )+ "\n");
Io.print((delete add.prototype )+ "\n");

Io.print((Object.prototype.propertyIsEnumerable.call(Object, "prototype") )+ "\n");
Io.print((delete Object.prototype )+ "\n");
