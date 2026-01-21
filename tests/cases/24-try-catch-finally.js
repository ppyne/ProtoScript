try {
    throw "err";
} catch (e) {
    Io.print((e )+ "\n");
} finally {
    Io.print(("done" )+ "\n");
}
