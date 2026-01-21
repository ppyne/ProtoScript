var out = 0;
outer: for (var i = 0; i < 3; i = i + 1) {
    for (var j = 0; j < 3; j = j + 1) {
        out = out + 1;
        break outer;
    }
}
Io.print((out )+ "\n");
