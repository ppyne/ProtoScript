Io.print("=== objects ===");

var o = new Object();
o.name = "ProtoScript";
o.year = 2026;
Io.print("name = " + o.name);
Io.print("year = " + o.year);

Io.print("hasOwnProperty(name) = " + o.hasOwnProperty("name"));

for (var k in o) {
    Io.print(k + "=" + o[k]);
}

Io.print("=== end objects ===");
