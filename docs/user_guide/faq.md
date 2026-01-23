# ProtoScript – Frequently Asked Questions

**How to get much time in milliseconds has passed since 00:00:00 UTC on 1 January 1970?**

```js
var ms = (new Date()).getTime();
```

**How to know if we are facing an Array or an Object?**

```js
function isArray($arr) {
    if ($arr instanceof Array) return true;
    return false;
}

function isObject($obj) {
    if ($obj instanceof Object) return true;
    return false;
}
```