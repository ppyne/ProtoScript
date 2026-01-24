include "RedBlackTree.js";

var rbt = new RedBlackTree();
rbt.insert(10);
rbt.insert(20);
rbt.insert(30);
rbt.insert(15);
rbt.insert(25);
rbt.insert(5);

Io.print(rbt.contains(15) ? "has 15\n" : "no 15\n");
Io.print(rbt.contains(99) ? "has 99\n" : "no 99\n");
Io.print(rbt.toArray().join(",") + "\n");
