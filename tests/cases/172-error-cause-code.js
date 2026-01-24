var e = new Error("boom", { cause: "root" });
Io.print((e.cause == "root") ? "cause ok\n" : "cause bad\n");

try {
    String.prototype.toString();
} catch (err) {
    Io.print((err.code == "ERR_INVALID_ARG") ? "code ok\n" : "code bad\n");
}
