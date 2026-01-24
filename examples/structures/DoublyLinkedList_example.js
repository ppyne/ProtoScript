include "DoublyLinkedList.js";

var list = new DoublyLinkedList();
list.push("b");
list.push("c");
list.prepend("a");

var found = list.find("b");
Io.print(found ? "found b\n" : "missing b\n");

list.remove("c");
Io.print("len=" + list.length + "\n");

var forward = list.toArray();
Io.print(forward.join(",") + "\n");

var backward = list.toArrayReverse();
Io.print(backward.join(",") + "\n");
