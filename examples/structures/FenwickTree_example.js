ProtoScript.include("FenwickTree.js");
var ft = new FenwickTree(6);
var values = [1, 3, 5, 7, 9, 11];
var i = 0;
while (i < values.length) {
    ft.update(i, values[i]);
    i++;
}

Io.print("sum(0,3)=" + ft.rangeQuery(0, 3) + "\n");
ft.update(1, 7);
Io.print("sum(0,3)=" + ft.rangeQuery(0, 3) + "\n");
