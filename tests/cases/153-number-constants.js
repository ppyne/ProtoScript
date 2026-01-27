function assert(cond, msg) {
    if (!cond) {
        throw msg;
    }
}

assert(typeof Infinity === "number", "Infinity type");
assert(1 / 0 === Infinity, "1/0");
assert(-1 / 0 === -Infinity, "-1/0");
assert(Number.POSITIVE_INFINITY === Infinity, "POSITIVE_INFINITY");
assert(Number.NEGATIVE_INFINITY === -Infinity, "NEGATIVE_INFINITY");
assert(Number.MAX_VALUE > 1e308, "MAX_VALUE");
assert(Number.MIN_VALUE > 0 && Number.MIN_VALUE < 1e-307, "MIN_VALUE");
assert(Number.NaN !== Number.NaN, "NaN");

Io.print("ok\n");
