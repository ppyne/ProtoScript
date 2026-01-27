ProtoScript.include("Stack.js");
var s = new Stack();
Io.print(s.isEmpty() ? "empty\n" : "not empty\n");

s.push("a");
s.push("b");
s.push("c");

Io.print("peek=" + s.peek() + "\n");
Io.print("len=" + s.length + "\n");

Io.print("pop=" + s.pop() + "\n");
Io.print("len=" + s.length + "\n");

var values = s.toArray();
Io.print(values.join(",") + "\n");
