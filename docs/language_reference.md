![ProtoScript](../header.png)

# ProtoScript Language Reference

This document lists the syntax and built-in symbols available to ProtoScript
developers in the current implementation. It is implementation-focused and
does not describe every ES1 rule in detail.

---

## 1. Lexical forms

### 1.1 Literals
- Numbers: decimal, hex (`0xFF`), octal (`077`), fractional, exponent (`1e3`)
- Strings: single or double quotes with escapes
- `true`, `false`, `null`, `undefined`
- RegExp literal: `/pattern/flags` (flags: `g`, `i`, `m` supported)

### 1.2 Identifiers
- ASCII letters, `_`, `$`, digits (not first), plus `\uXXXX` escapes
- Raw non-ASCII letters are accepted by the lexer

---

## 2. Keywords

```
var if else while do for in of switch case default function return
break continue with try catch finally throw new true false null typeof
instanceof void delete
```

---

## 3. Operators and punctuation

### 3.1 Unary
`+ - ! ~ typeof void delete ++ --`

### 3.2 Binary
`+ - * / %`
`< <= > >= instanceof`
`== != === !==`
`& | ^ << >> >>>`
`&& ||`

### 3.3 Assignment
`= += -= *= /= %= &= |= ^= <<= >>= >>>=`

### 3.4 Conditional / comma
`?: ,`

### 3.5 Punctuation
`() [] {} . ; : ? ,`

---

## 4. Statements

- `if / else`
- `while`, `do...while`
- `for (init; test; update)`
- `for (x in obj)`
- `for (x of iterable)` (ES6-style simplified)
- `switch / case / default`
- `break`, `continue`, labels
- `try / catch / finally`, `throw`
- `return`
- `with` (compile-time gated)

---

## 5. Global values

- `undefined` (read-only)
- `NaN` (read-only)

---

## 6. Global constructors and objects

### 6.1 Object
Constructor: `Object(value)`
Prototype methods:
- `toString()`
- `toLocaleString()`
- `valueOf()`
- `hasOwnProperty(name)`
- `propertyIsEnumerable(name)`
- `isPrototypeOf(obj)`

### 6.2 Function
Constructor: `Function(...)`
Prototype methods:
- `call(thisArg, ...args)`
- `apply(thisArg, arrayLike)`
- `bind(thisArg, ...args)`
- `toString()`
- `valueOf()`

### 6.3 Boolean
Constructor: `Boolean(value)`
Prototype methods:
- `toString()`
- `valueOf()`

### 6.4 Number
Constructor: `Number(value)`
Prototype methods:
- `toString()`
- `valueOf()`
- `toFixed(fractionDigits)`
- `toExponential(fractionDigits)`
- `toPrecision(precision)`

### 6.5 String
Constructor: `String(value)`
Static:
- `String.fromCharCode(...codePoints)`
Prototype methods:
- `toString()`
- `valueOf()`
- `charAt(index)`
- `charCodeAt(index)`
- `indexOf(needle, fromIndex)`
- `lastIndexOf(needle, fromIndex)`
- `substring(start, end)`
- `slice(start, end)`
- `concat(...parts)`
- `split(sep, limit)`
- `replace(search, replacement)`
- `match(regexp)`
- `search(regexp)`

### 6.6 Array
Constructor: `Array(length|...items)`
Prototype methods:
- `toString()`
- `join(sep)`
- `push(...items)`
- `pop()`
- `shift()`
- `unshift(...items)`
- `slice(start, end)`
- `concat(...items)`
- `reverse()`
- `sort(compareFn)`
- `splice(start, deleteCount, ...items)`

### 6.7 Date
Constructor: `Date(...)` / `new Date(...)`
Static:
- `Date.parse(string)`
- `Date.UTC(...)`
Prototype methods:
- `toString()`
- `getTime()`
- `getFullYear()`, `getMonth()`, `getDate()`, `getDay()`
- `getHours()`, `getMinutes()`, `getSeconds()`, `getMilliseconds()`
- `setFullYear(y, m, d)`
- `setMonth(m, d)`
- `setDate(d)`
- `setHours(h, m, s, ms)`
- `setMinutes(m, s, ms)`
- `setSeconds(s, ms)`
- `setMilliseconds(ms)`

### 6.8 RegExp
Constructor: `RegExp(pattern, flags)`
Prototype methods:
- `toString()`
- `exec(string)`
- `test(string)`
Static captures:
- `RegExp["$1"]` … `RegExp["$9"]`

### 6.9 Math
Object: `Math`
Constants:
`E`, `LN10`, `LN2`, `LOG2E`, `LOG10E`, `PI`, `SQRT1_2`, `SQRT2`
Functions:
`abs`, `acos`, `asin`, `atan`, `atan2`, `ceil`, `cos`, `exp`, `floor`, `log`,
`max`, `min`, `pow`, `round`, `sin`, `sqrt`, `tan`, `random`

### 6.10 JSON (ES5)
Object: `JSON`
- `JSON.parse(text)`
- `JSON.stringify(value)`

### 6.11 Errors
Constructors:
`Error`, `TypeError`, `RangeError`, `ReferenceError`, `SyntaxError`, `EvalError`

---

## 7. Host extensions

### 7.1 Io
Object: `Io`
- `Io.print(value)`
- `Io.open(path, mode)`
- `Io.read(file)`
- `Io.readLines(file)`
- `Io.write(file, data)`
- `Io.writeLine(file, data)`
- `Io.close(file)`
- `Io.tempPath()`
- `Io.EOL`
- `Io.stdin`, `Io.stdout`, `Io.stderr`

### 7.2 Gc
Object: `Gc`
- `Gc.collect()`
- `Gc.stats()`

---

## 8. Compile-time flags

Defined in `include/ps_config.h` (0 = disabled, 1 = enabled):
- `PS_ENABLE_WITH`
- `PS_ENABLE_EVAL`
- `PS_ENABLE_ARGUMENTS_ALIASING`
