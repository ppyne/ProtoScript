include "DisjointSet.js";

var ds = new DisjointSet();
ds.union("a", "b");
ds.union("b", "c");

Io.print(ds.connected("a", "c") ? "a~c\n" : "a!c\n");
Io.print(ds.connected("a", "d") ? "a~d\n" : "a!d\n");

ds.union("d", "e");
Io.print(ds.connected("d", "e") ? "d~e\n" : "d!e\n");
