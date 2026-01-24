function inner() {
    throw new Error("boom");
}

function outer() {
    inner();
}

try {
    outer();
} catch (e) {
    var ok_stack = (typeof e.stack == "string") && e.stack.length > 0;
    var ok_file = (typeof e.file == "string") && e.file.indexOf("171-error-stack.js") >= 0;
    var ok_names = e.stack.indexOf("inner") >= 0 && e.stack.indexOf("outer") >= 0;
    Io.print(ok_stack ? "stack ok\n" : "stack bad\n");
    Io.print(ok_file ? "file ok\n" : "file bad\n");
    Io.print(ok_names ? "names ok\n" : "names bad\n");
}
