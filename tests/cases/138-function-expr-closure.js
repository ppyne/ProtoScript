function add(a) {
    return function(b) {
        return a + b;
    };
}
var add2 = add(2);
Io.print(add2(3) + "\n");
