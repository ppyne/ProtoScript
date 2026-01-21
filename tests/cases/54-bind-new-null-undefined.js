function RetNull() {
    this.x = 1;
    return null;
}
function RetUndef() {
    this.y = 2;
}
var B1 = RetNull.bind(null);
var B2 = RetUndef.bind(null);
var i1 = new B1();
var i2 = new B2();
Io.print(i1.x);
Io.print(i2.y);
