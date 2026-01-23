function f() { return this; }
var o = { x: 1, f: f };
Io.print((o.f() === o) + "\n");
