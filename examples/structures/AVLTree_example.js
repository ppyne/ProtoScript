ProtoScript.include("AVLTree.js");
var avl = new AVLTree();
avl.insert(10);
avl.insert(20);
avl.insert(30);
avl.insert(40);
avl.insert(50);
avl.insert(25);

Io.print(avl.contains(25) ? "has 25\n" : "no 25\n");
Io.print(avl.contains(99) ? "has 99\n" : "no 99\n");
Io.print(avl.toArray().join(",") + "\n");
