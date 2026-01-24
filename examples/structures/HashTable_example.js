include "HashTable.js";

var table = new HashTable(8);
Io.print(table.has("a") ? "has a\n" : "no a\n");

table.set("a", 1);
table.set("b", 2);
table.set("c", 3);
Io.print("len=" + table.length + "\n");

Io.print("b=" + table.get("b") + "\n");
table.set("b", 20);
Io.print("b=" + table.get("b") + "\n");

table.remove("a");
Io.print(table.has("a") ? "has a\n" : "no a\n");

var keys = table.keys();
Io.print(keys.join(",") + "\n");
