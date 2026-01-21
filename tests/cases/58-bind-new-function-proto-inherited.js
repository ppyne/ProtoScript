var _save = Object.prototype.base;
Object.prototype.base = "base";
function RetFuncProtoInherited() {
    function inner() {
        return 4;
    }
    var proto = new Object();
    proto.child = "child";
    inner.prototype = proto;
    return inner;
}
var B = RetFuncProtoInherited.bind(null);
var inst = new B();
Io.print((inst.prototype.base )+ "\n");
Io.print((inst.prototype.child )+ "\n");
Io.print((inst() )+ "\n");
Object.prototype.base = _save;
