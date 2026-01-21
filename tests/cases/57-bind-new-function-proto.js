function RetFuncProto() {
    function inner() {
        return 3;
    }
    inner.prototype = new Object();
    inner.prototype.tag = "ok";
    return inner;
}
var B = RetFuncProto.bind(null);
var inst = new B();
Io.print(typeof inst);
Io.print(inst.prototype.tag);
Io.print(inst());
