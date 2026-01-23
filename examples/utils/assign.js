// Shallow assign helper (ES5/ES2015 style): copies enumerable own properties.
// Limitations: no Symbols, no property descriptors (values only).
function assign(target) {
    if (target == null) {
        throw new TypeError("assign target is null or undefined");
    }
    var to = Object(target);
    for (var i = 1; i < arguments.length; i++) {
        var src = arguments[i];
        if (src == null) {
            continue;
        }
        var from = Object(src);
        for (var key in from) {
            if (from.hasOwnProperty(key)) {
                to[key] = from[key];
            }
        }
    }
    return to;
}
