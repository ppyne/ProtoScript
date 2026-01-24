include "Queue.js";

var q = new Queue();
Io.print(q.isEmpty() ? "empty\n" : "not empty\n");

q.enqueue("a");
q.enqueue("b");
q.enqueue("c");

Io.print("peek=" + q.peek() + "\n");
Io.print("len=" + q.length + "\n");

Io.print("deq=" + q.dequeue() + "\n");
Io.print("len=" + q.length + "\n");

var values = q.toArray();
Io.print(values.join(",") + "\n");
