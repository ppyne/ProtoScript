include "BinarySearchTree.js";

var bst = new BinarySearchTree();
bst.insert(5);
bst.insert(3);
bst.insert(8);
bst.insert(1);
bst.insert(4);

Io.print(bst.contains(4) ? "has 4\n" : "no 4\n");
Io.print(bst.contains(6) ? "has 6\n" : "no 6\n");

bst.remove(3);
Io.print("len=" + bst.toArray().length + "\n");
Io.print(bst.toArray().join(",") + "\n");
