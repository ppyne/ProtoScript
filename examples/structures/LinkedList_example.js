include "LinkedList.js";

var list = new LinkedList();
list.push("a");
list.push("b");
list.prepend("start");

var found = list.find("b");
Io.print(found ? "found b\n" : "missing b\n");

list.remove("a");
Io.print("len=" + list.length + "\n");

var values = list.toArray();
Io.print(values.join(",") + "\n");
