try {
    String.prototype.toString();
} catch (e) {
    Io.print((e.name )+ "\n");
}
try {
    Number.prototype.valueOf();
} catch (e) {
    Io.print((e.name )+ "\n");
}
try {
    Boolean.prototype.toString();
} catch (e) {
    Io.print((e.name )+ "\n");
}
try {
    (null).toString();
} catch (e) {
    Io.print((e.name )+ "\n");
}
try {
    (void 0).toString();
} catch (e) {
    Io.print((e.name )+ "\n");
}
