var obj = JSON.parse('{"a":1,"b":true,"c":null,"d":"hi","e":[1,2,"x"]}');
Io.print((obj.a) + "\n");
Io.print((obj.b) + "\n");
Io.print((obj.c) + "\n");
Io.print((obj.d) + "\n");
Io.print((obj.e.length) + "\n");
Io.print((obj.e[2]) + "\n");
var s = JSON.parse('"a\\\"b\\\\c"');
Io.print((s) + "\n");
try {
    JSON.parse('{"a":1,}');
} catch (e) {
    Io.print((e.name) + "\n");
}
