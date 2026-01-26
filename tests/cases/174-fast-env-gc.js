function keepAlive() {
    var obj = { value: 42 };
    Gc.collect();
    return obj.value === 42 ? "OK" : "FAIL";
}

Io.print(keepAlive() + "\n");
