function RetFuncProps() {
    function inner() {
        return 2;
    }
    inner.value = 9;
    return inner;
}
var B = RetFuncProps.bind(null);
var inst = new B();
Io.print((inst.value )+ "\n");
Io.print((inst() )+ "\n");
