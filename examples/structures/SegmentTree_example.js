include "SegmentTree.js";

var values = [1, 3, 5, 7, 9, 11];
var seg = new SegmentTree(values);

Io.print("sum(1,3)=" + seg.query(1, 3) + "\n");
seg.update(1, 10);
Io.print("sum(1,3)=" + seg.query(1, 3) + "\n");
