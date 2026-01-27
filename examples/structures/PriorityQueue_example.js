ProtoScript.include("PriorityQueue.js");
var pq = new PriorityQueue();
pq.enqueue(5);
pq.enqueue(1);
pq.enqueue(3);
pq.enqueue(2);

Io.print("size=" + pq.size() + "\n");
Io.print("peek=" + pq.peek() + "\n");
Io.print("deq=" + pq.dequeue() + "\n");
Io.print("deq=" + pq.dequeue() + "\n");
Io.print("size=" + pq.size() + "\n");

var maxPQ = new PriorityQueue(function (a, b) {
    if (a > b) return -1;
    if (a < b) return 1;
    return 0;
});
maxPQ.enqueue(5);
maxPQ.enqueue(1);
maxPQ.enqueue(3);
Io.print("max=" + maxPQ.dequeue() + "\n");
