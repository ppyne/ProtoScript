function RetFunc() {
    function inner() {
        return 1;
    }
    return inner;
}
var B = RetFunc.bind(null);
var inst = new B();
Io.print((typeof inst )+ "\n");
Io.print((inst() )+ "\n");
