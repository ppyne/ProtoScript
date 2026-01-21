Io.print("=== objects ===" + "\n");

var o = new Object();
o.name = "ProtoScript";
o.year = 2026;
Io.print("name = " + o.name + "\n");
Io.print("year = " + o.year + "\n");

Io.print("hasOwnProperty(name) = " + o.hasOwnProperty("name") + "\n");

for (var k in o) {
    Io.print(k + "=" + o[k] + "\n");
}

Io.print("=== end objects ===" + "\n");
