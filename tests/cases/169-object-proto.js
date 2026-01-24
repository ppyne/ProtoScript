function Base() {
    this.x = 1;
}

Base.prototype.y = 2;

function Child() {
    this.z = 3;
}

Child.prototype = new Base();
Child.prototype.constructor = Child;

var total = 0;
for (var i = 0; i < 5000; i++) {
    var o = new Child();
    total += o.x + o.y + o.z;
    if ((i % 1000) === 0) {
        o.extra = i;
        total += o.extra;
        Gc.collect();
    }
}

if (total <= 0) {
    Io.print("bad\n");
} else {
    Io.print("OK\n");
}
