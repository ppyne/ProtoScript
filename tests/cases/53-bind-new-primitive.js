function RetPrim() {
    return 5;
}
var B = RetPrim.bind(null);
var inst = new B();
Io.print((typeof inst )+ "\n");
Io.print((inst.toString() )+ "\n");
