# Chapter 2 — Values, Variables, and Literals

This chapter describes the **fundamental data elements** of ProtoScript: values, variables, and literals.

---

## Values

ProtoScript recognizes the following primitive values:

- **Numbers**, such as `42` or `3.14159`
- **Boolean values**, either `true` or `false`
- **Strings**, such as `"Howdy!"`
- **`null`**, a special value representing no value
- **`undefined`**, a primitive value indicating absence of initialization

There is no distinction between integer and floating-point numeric types. Dates are handled using the `Date` object rather than a primitive type.

Objects and functions are also fundamental elements of the language. Objects act as containers for values, and functions define executable procedures.

---

## Data Type Conversion

ProtoScript is a **dynamically typed language**. Variables are not bound to a specific type, and values are converted automatically as needed during execution.

```js
var answer = 42;
answer = "Thanks for all the fish...";
```

In expressions using the `+` operator, numeric values are converted to strings when combined with strings:

```js
"The answer is " + 42;   // "The answer is 42"
42 + " is the answer";   // "42 is the answer"
```

Other operators do not perform this string conversion:

```js
"37" - 7;   // 30
"37" + 7;   // "377"
```

---

## Variables

Variables are symbolic names for values. Identifiers must:

- start with a letter, underscore (`_`), or `$`
- contain letters, digits (`0–9`), underscores, or `$`
- be case-sensitive

Examples of valid identifiers:

```js
Number_hits
temp99
_name
$total
```

---

## Declaring Variables

Variables may be declared in two ways:

- by assignment:

```js
x = 42;
```

- using the `var` keyword:

```js
var x = 42;
```

Using `var` is **required inside functions** and optional at the top level.

---

## Evaluating Variables

An unassigned variable has the value `undefined`. The result of evaluating such a variable depends on how it was declared:

- without `var` → runtime error
- with `var` → evaluates to `undefined` (or `NaN` in numeric contexts)

Example:

```js
function f1() {
    return y - 2;
}
// runtime error
```

```js
function f2() {
    var y;
    return y - 2;
}
// NaN
```

Testing for `undefined`:

```js
var input;
if (input === undefined) {
    doThis();
} else {
    doThat();
}
```

The value `undefined` behaves as `false` in Boolean contexts.

The value `null` behaves as `0` in numeric contexts and as `false` in Boolean contexts:

```js
var n = null;
n * 32;   // 0
```

---

## Variable Scope

Variables declared outside a function are **global**. Variables declared inside a function using `var` are **local** to that function.

```js
var globalVar = 1;

function example() {
    var localVar = 2;
}
```

All browser-specific scope mechanisms (frames, windows) are intentionally excluded.

---

## Literals

Literals are fixed values written directly into source code.

### Array Literals

```js
coffees = ["French Roast", "Columbian", "Kona"];
```

Array literals create a new array each time they are evaluated.

Extra commas create empty elements:

```js
fish = ["Lion", , "Angel"]; // fish[1] is undefined
```

---

### Boolean Literals

```js
true
false
```

---

### Floating-Point Literals

Examples:

```js
3.1415
-3.1E12
.1e12
2E-12
```

---

### Integer Literals

Integers may be written in:

- decimal (base 10)
- hexadecimal (base 16, `0x` prefix)
- octal (base 8, leading `0`)
- exponent notation (base-10, `e`/`E` with optional sign)

Examples:

```js
42
0xFFF
012
-345
2E-12
```

---

### Object Literals

```js
var Sales = "Toyota";

function CarTypes(name) {
    if (name == "Honda")
        return name;
    else
        return "Sorry, we don't sell " + name + ".";
}

car = {
    myCar: "Saturn",
    getCar: CarTypes("Honda"),
    special: Sales
};
```

---

### String Literals

Strings are enclosed in single or double quotes:

```js
"hello"
'world'
"one line \n another line"
```

String literals are automatically converted to temporary `String` objects when methods are invoked.

---

## Special Characters in Strings

| Escape | Meaning |
|-------|---------|
| `\b` | backspace |
| `\f` | form feed |
| `\n` | newline |
| `\r` | carriage return |
| `\t` | tab |
| `\'` | single quote |
| `\"` | double quote |
| `\\` | backslash |
| `\xXX` | Latin-1 character |
| `\uXXXX` | Unicode character |

---

## Unicode

ProtoScript supports Unicode characters and they may appear in:

- string literals
- comments
- identifiers (including raw Unicode letters and `\uXXXX` escapes)

### Unicode compatibility with ASCII and ISO

ASCII text is a strict subset of Unicode, so ASCII source code works unchanged.
Latin-1 text (ISO-8859-1) can be represented directly in UTF-8 source files or
via `\xXX` escapes in string literals.

### Glyph-based UTF-8 model

ProtoScript uses a glyph-based UTF-8 string model (not ES1/JS1.x UTF-16).
`length` counts glyphs and `charCodeAt` returns full Unicode code points.

### Unicode escape sequences

Unicode escape sequences use the form:

```js
"\u00A9"
```

This represents a single Unicode character.

### Displaying Unicode characters

Unicode characters can be included directly in source files saved as UTF-8:

```js
Io.print("cafe\u0301\n");
Io.print("café\n");
```

Both lines print the same visible text if your terminal supports combining
characters.
