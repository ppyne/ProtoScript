![ProtoScript](../header.png)

# ProtoScript — ES1 Compatibility Report

This report describes how the current ProtoScript implementation aligns with
ECMA-262 Edition 1 (June 1997). It is based on:
- `docs/language_reference.md`
- `docs/es1-notes.md`
- the current source implementation

If a feature is not listed as implemented below, it is not implemented in the
current build.

---

## 1. Summary

- Core ES1 syntax and most standard objects are implemented.
- Known deviations: UTF-8 glyph string model, limited RegExp ignoreCase, optional
  eval/with, optional arguments aliasing.
- ES1 globals and global functions are implemented (see Section 4).
- Extra non-ES1 features are present (include directive, for...of, default
  parameters, JSON, Object prototype APIs, host modules).

---

## 2. Language syntax coverage

### 2.1 Lexical forms

- Numbers: decimal, hex (`0x`), octal (`0` prefix), binary (`0b` / `0B`).
  Binary literals are an ES2015 extension.
- Strings: single or double quotes with standard escapes.
- RegExp literals: `/pattern/flags` with `g`, `i`, `m`.
- Identifiers: ASCII letters, `_`, `$`, digits (not first), `\uXXXX` escapes,
  and raw non-ASCII bytes.

### 2.2 Keywords and statements

Keywords (from the lexer):
```
var if else while do for in of switch case default function return
break continue with try catch finally throw new true false null typeof
instanceof void delete include
```

Statements:
- `if / else`, `while`, `do...while`, `for`, `for...in`
- `for...of` (ES2015 extension; simplified iterator model)
- `switch / case / default`
- `break`, `continue`, labels
- `try / catch / finally`, `throw`
- `return`
- `with` (compile-time gated)
- `include "path.js"` (non-ES1, top-level only)

### 2.3 Operators

Unary:
`+ - ! ~ typeof void delete ++ --`

Binary:
`+ - * / %`
`< <= > >= instanceof in`
`== != === !==`
`& | ^ << >> >>>`
`&& ||`

Assignment:
`= += -= *= /= %= &= |= ^= <<= >>= >>>=`

Other:
`?: ,`

Notes:
- `?:` is the ternary (conditional) operator.
- `,` is the comma operator, which evaluates expressions left-to-right and
  yields the value of the last expression.

---

## 3. Deviations and behavior differences

From `docs/es1-notes.md` plus current implementation checks:

1. String model: UTF-8 glyph-based length and indexing (not UTF-16 code units).
2. RegExp ignoreCase uses a limited static case-folding table.
3. `arguments` <-> parameter aliasing is optional
   (`PS_ENABLE_ARGUMENTS_ALIASING=1`).
4. `eval` and `with` are compile-time gated (`PS_ENABLE_EVAL`,
   `PS_ENABLE_WITH`).
5. Static `include "path.js"` directive is non-ES1.
6. Host extensions: `Io`, `Fs`, `Buffer`, `Event`, `Display`, `Image`, `Gc`,
   `ProtoScript`, and `console`.
7. `Gc` module is non-standard.
8. ES2015 default parameter values are supported.
9. Object prototype APIs beyond ES1: `Object.getPrototypeOf`,
   `Object.create`, `Object.setPrototypeOf`.
10. `JSON.parse` / `JSON.stringify` (ES5) are available with limitations.
11. `for...of` is supported (ES2015 extension).
12. `instanceof` is supported (ES3 extension).
13. Array literal elisions and trailing commas behave as ES1.
14. Binary integer literals (`0b...`) are supported (ES2015 extension).

---

## 4. ES1 globals and standard library coverage

### 4.1 Global values

Implemented:
- `undefined`
- `NaN`

Notes:
- `Infinity` is supported when parsing numbers (e.g. `Number("Infinity")`), but
  there is no global `Infinity` property yet.

### 4.2 Global functions

Implemented:
- `eval` (compile-time gated via `PS_ENABLE_EVAL`)
- `isFinite`
- `isNaN`
- `parseInt`
- `parseFloat`
- `escape`
- `unescape`

### 4.3 Core constructors and objects (implemented surface)

Object:
- `Object(value)`
- `Object.getPrototypeOf(obj)` (ES5)
- `Object.setPrototypeOf(obj, proto)` (ES2015)
- `Object.create(proto)` (ES5, no property descriptors)
- `Object.prototype.toString`
- `Object.prototype.toLocaleString`
- `Object.prototype.valueOf`
- `Object.prototype.hasOwnProperty`
- `Object.prototype.propertyIsEnumerable`
- `Object.prototype.isPrototypeOf`

Function:
- `Function(...)`
- `Function.prototype.call`
- `Function.prototype.apply`
- `Function.prototype.bind` (ES5)
- `Function.prototype.toString`
- `Function.prototype.valueOf`

Boolean:
- `Boolean(value)`
- `Boolean.prototype.toString`
- `Boolean.prototype.valueOf`

Number:
- `Number(value)`
- `Number.prototype.toString`
- `Number.prototype.valueOf`
- `Number.prototype.toFixed`
- `Number.prototype.toExponential`
- `Number.prototype.toPrecision`

String:
- `String(value)`
- `String.fromCharCode`
- `String.prototype.toString`
- `String.prototype.valueOf`
- `String.prototype.charAt`
- `String.prototype.charCodeAt`
- `String.prototype.indexOf`
- `String.prototype.lastIndexOf`
- `String.prototype.substring`
- `String.prototype.slice`
- `String.prototype.concat`
- `String.prototype.split`
- `String.prototype.replace`
- `String.prototype.match`
- `String.prototype.search`

Array:
- `Array(length|...items)`
- `Array.prototype.toString`
- `Array.prototype.join`
- `Array.prototype.push`
- `Array.prototype.pop`
- `Array.prototype.shift`
- `Array.prototype.unshift`
- `Array.prototype.slice`
- `Array.prototype.concat`
- `Array.prototype.reverse`
- `Array.prototype.sort`
- `Array.prototype.splice`

Date:
- `Date(...)` / `new Date(...)`
- `Date.parse`
- `Date.UTC`
- `Date.prototype.toString`
- `Date.prototype.getTime`
- `Date.prototype.getFullYear`, `getMonth`, `getDate`, `getDay`
- `Date.prototype.getHours`, `getMinutes`, `getSeconds`, `getMilliseconds`
- `Date.prototype.setFullYear`, `setMonth`, `setDate`
- `Date.prototype.setHours`, `setMinutes`, `setSeconds`, `setMilliseconds`

RegExp:
- `RegExp(pattern, flags)`
- `RegExp.prototype.toString`
- `RegExp.prototype.exec`
- `RegExp.prototype.test`
- `RegExp["$1"]` .. `RegExp["$9"]`

Math:
- constants: `E`, `LN10`, `LN2`, `LOG2E`, `LOG10E`, `PI`, `SQRT1_2`, `SQRT2`
- functions: `abs`, `acos`, `asin`, `atan`, `atan2`, `ceil`, `cos`, `exp`,
  `floor`, `log`, `max`, `min`, `pow`, `round`, `sin`, `sqrt`, `tan`, `random`

JSON (ES5 extension):
- `JSON.parse(text)`
- `JSON.stringify(value)`

Errors:
- `Error`, `TypeError`, `RangeError`, `ReferenceError`, `SyntaxError`,
  `EvalError`

---

## 5. Host extensions (non-ES1)

Global objects beyond ES1:
- `ProtoScript` (`args`, `version`, `exit`, `sleep`, `usleep`)
- `Io` (`print`, `sprintf`, file I/O, streams)
- `Fs` (POSIX filesystem helpers)
- `Buffer` (byte buffers)
- `Event` (polling events)
- `Image` (PNG/JPEG decode + resample)
- `Display` (SDL-backed framebuffer)
- `Gc` (GC control)
- `console` (`log`, `info`, `warn`, `error`)

---

## 6. Compile-time gates

Defined in `include/ps_config.h`:
- `PS_ENABLE_WITH` (enables `with`)
- `PS_ENABLE_EVAL` (enables `eval`)
- `PS_ENABLE_ARGUMENTS_ALIASING`
- `PS_ENABLE_MODULE_FS`
- `PS_ENABLE_MODULE_DISPLAY`
- `PS_ENABLE_MODULE_IMG`
