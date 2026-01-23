/*
Deep clone helper for ProtoScript.

What it does:
- Clones objects and arrays recursively (deep).
- Preserves cycles (same object referenced twice stays shared).
- Copies own enumerable properties only.
- Preserves prototypes when Object.getPrototypeOf/Object.create are available.
- Handles Date, RegExp, and Buffer specially when present.

What it does NOT do (limitations):
- No property descriptors (writable/enumerable/configurable) are preserved.
- Non-enumerable properties are not copied.
- Functions are not cloned; references are reused.
- Custom host objects beyond Date/RegExp/Buffer are not cloned specially.
- If Object.getPrototypeOf/Object.create are missing, prototypes are lost
  (arrays become [] and objects become {} with copied properties).

Expected use:
Use for data objects/arrays where deep value copying is needed. Do not rely on
it for exact engine-level cloning or for preserving hidden/internal state.
*/
function clone(obj) {
    var seen = [];
    var copies = [];

    function canUsePrototypeApi() {
        return (typeof Object == "function" &&
            Object &&
            typeof Object.getPrototypeOf == "function" &&
            typeof Object.create == "function");
    }

    function getProto(value) {
        if (!canUsePrototypeApi()) {
            return null;
        }
        return Object.getPrototypeOf(value);
    }

    function isBuffer(value) {
        if (typeof Buffer != "object" || !Buffer || typeof Buffer.size != "function") {
            return false;
        }
        try {
            Buffer.size(value);
            return true;
        } catch (e) {
            return false;
        }
    }

    function cloneBuffer(value) {
        var size = Buffer.size(value);
        var out = Buffer.alloc(size);
        for (var i = 0; i < size; i++) {
            out[i] = value[i];
        }
        return out;
    }

    function cloneRegExp(value) {
        var flags = "";
        if (value.global) flags += "g";
        if (value.ignoreCase) flags += "i";
        if (value.multiline) flags += "m";
        return new RegExp(value.source, flags);
    }

    function cloneInner(value) {
        if (value == null) {
            return value;
        }
        if (typeof value != "object") {
            return value;
        }
        if (typeof Date == "function" && value instanceof Date) {
            return new Date(value.getTime());
        }
        if (typeof RegExp == "function" && value instanceof RegExp) {
            return cloneRegExp(value);
        }
        if (isBuffer(value)) {
            return cloneBuffer(value);
        }

        for (var i = 0; i < seen.length; i++) {
            if (seen[i] === value) {
                return copies[i];
            }
        }

        var out;
        var proto = getProto(value);
        if (value instanceof Array) {
            if (proto && canUsePrototypeApi() && proto !== Array.prototype) {
                out = Object.create(proto);
            } else {
                out = [];
            }
        } else if (canUsePrototypeApi()) {
            out = Object.create(proto);
        } else {
            out = {};
        }
        seen[seen.length] = value;
        copies[copies.length] = out;

        for (var key in value) {
            if (value.hasOwnProperty(key)) {
                out[key] = cloneInner(value[key]);
            }
        }
        if (value instanceof Array) {
            out.length = value.length;
        }
        return out;
    }

    return cloneInner(obj);
}
