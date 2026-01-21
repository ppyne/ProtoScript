function Foo() {}
var a = new Foo();
Io.print((a instanceof Foo) + "\n");
Io.print(({} instanceof Foo) + "\n");
Io.print((1 instanceof Foo) + "\n");

var obj = {};
try {
    Io.print((a instanceof obj) + "\n");
} catch (e) {
    Io.print(e.name + "\n");
}

function Base() {}
function Child() {}
Child.prototype = new Base();
var c = new Child();
Io.print((c instanceof Base) + "\n");
