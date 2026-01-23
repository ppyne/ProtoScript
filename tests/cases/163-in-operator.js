var obj = { a: 1 };
Io.print(("a" in obj) + "\n");
Io.print(("b" in obj) + "\n");

function Foo() {}
Foo.prototype.x = 1;
var f = new Foo();
Io.print(("x" in f) + "\n");

Io.print((("length") in "hi") + "\n");
