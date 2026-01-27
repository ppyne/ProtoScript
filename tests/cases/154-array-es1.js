function assert(cond, msg) {
    if (!cond) {
        throw msg;
    }
}

var x = [0, 1, 2, 3];
assert(x.join(undefined) === "0,1,2,3", "join undefined");

var s1 = x.slice(0, NaN);
assert(s1.length === 0, "slice end NaN");

var s2 = x.slice(NaN);
assert(s2.length === 4, "slice start NaN");

var y = [0, 1, 2, 3];
var sp = y.splice(0, undefined);
assert(sp.length === 0, "splice deleteCount undefined");
assert(y.length === 4 && y[0] === 0, "splice no delete");

Io.print("ok\n");
