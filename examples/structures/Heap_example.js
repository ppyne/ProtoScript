ProtoScript.include("Heap.js");
var h = new Heap();
h.push(5);
h.push(3);
h.push(8);
h.push(1);

Io.print("size=" + h.size() + "\n");
Io.print("peek=" + h.peek() + "\n");

Io.print("pop=" + h.pop() + "\n");
Io.print("pop=" + h.pop() + "\n");
Io.print("size=" + h.size() + "\n");

var maxHeap = new Heap(function (a, b) {
    if (a > b) return -1;
    if (a < b) return 1;
    return 0;
});

maxHeap.push(5);
maxHeap.push(3);
maxHeap.push(8);
maxHeap.push(1);

Io.print("max=" + maxHeap.pop() + "\n");
