var o = {};
var f = function () {
    return foo;
};

with (o) {
    var foo = "ok";
}

Io.print(f() + "\n");
