function f(x, y = 10) {
    return x + y;
}
Io.print(f(5));
Io.print(f(5, 2));
Io.print(f(5, undefined));
function g(x, y = x + 1) {
    return y;
}
Io.print(g(2));
