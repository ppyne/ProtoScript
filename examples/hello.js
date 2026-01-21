Io.print("=== ProtoScript demo ===" + "\n");

var name = "ProtoScript";
var year = 2026;

Io.print("Hello " + name + "\n");
Io.print("Year: " + year + "\n");

/* Function */
function add(a, b) {
    return a + b;
}

var result = add(3, 4);
Io.print("3 + 4 = " + result + "\n");

/* while loop */
var i = 0;
while (i < 3) {
    Io.print("while i = " + i + "\n");
    i = i + 1;
}

/* for loop */
for (var j = 0; j < 3; j = j + 1) {
    Io.print("for j = " + j + "\n");
}

Io.print("=== end ===" + "\n");
