![ProtoScript](../../header.png)

# Chapter 6 — Functions

Functions are one of the fundamental building blocks in **ProtoScript**. A function is a procedure—a set of statements that performs a specific task. To use a function, you must first define it; then your script can call it.

---

## Defining Functions

A function definition consists of:

- The `function` keyword
- The name of the function
- A list of parameters, enclosed in parentheses and separated by commas
- A block of statements enclosed in curly braces `{ }`

ProtoScript also supports ES2015-style default parameter values:

```js
function f(x, y = 10) {
    return x + y;
}
```

```js
function square(number) {
    return number * number;
}
```

The function `square` takes one parameter, `number`, and returns its value multiplied by itself. The `return` statement specifies the value returned by the function.

```js
return number * number;
```

### Argument Passing

All parameters are passed to functions **by value**. If a function changes the value of a parameter, that change is not reflected outside the function.

If an object is passed as a parameter and the function modifies the object’s properties, the change is visible outside the function:

```js
function myFunc(theObject) {
    theObject.make = "Toyota";
}

mycar = { make: "Honda", model: "Accord", year: 1998 };
x = mycar.make;      // "Honda"
myFunc(mycar);
y = mycar.make;      // "Toyota"
```

Functions can also be created dynamically using the `Function` object (described in the [Working with Objects](working_with_objects.md) chapter).

A **method** is a function associated with an object.

---

## Calling Functions

Defining a function does not execute it. Calling a function executes its statements using the supplied arguments.

```js
square(5); // returns 25
```

Function arguments are not limited to primitive values; objects can also be passed as arguments.

### Recursive Functions

A function may call itself. This is known as recursion.

```js
function factorial(n) {
    if ((n == 0) || (n == 1))
        return 1;
    else
        return n * factorial(n - 1);
}
```

```js
a = factorial(1); // 1
b = factorial(2); // 2
c = factorial(3); // 6
d = factorial(4); // 24
e = factorial(5); // 120
```

---

## Using the `arguments` Array

Within a function, arguments passed to it are available through the `arguments` array.

```js
arguments[i]
```

The index `i` starts at zero. The total number of arguments passed is available as `arguments.length`.

This allows functions to accept a variable number of arguments.

Note: full parameter ↔ arguments aliasing is compile-time gated by
`PS_ENABLE_ARGUMENTS_ALIASING` (default `0` in `include/ps_config.h`). When
disabled, `arguments[i]` does not update the corresponding named parameter and
vice versa.

### Example

```js
function myConcat(separator) {
    var result = "";
    for (var i = 1; i < arguments.length; i++) {
        result += arguments[i] + separator;
    }
    return result;
}
```

```js
myConcat(", ", "red", "orange", "blue");
myConcat("; ", "elephant", "giraffe", "lion", "cheetah");
myConcat(". ", "sage", "basil", "oregano", "pepper", "parsley");
```

---

## Predefined Functions

ProtoScript provides several predefined top-level functions:

- `eval`
- `isFinite`
- `isNaN`
- `parseInt`
- `parseFloat`
- `Number`
- `String`

### `eval`

Evaluates a string of ProtoScript code.

```js
eval(expr);
```

If `expr` represents an expression, it is evaluated. If it represents statements, they are executed.

Availability: compile-time gated by `PS_ENABLE_EVAL` (default `0` in
`include/ps_config.h`). When disabled, calling `eval(...)` throws an
`EvalError` with the message `eval is disabled`.

---

### `isFinite`

Determines whether a value is a finite number.

```js
isFinite(number);
```

Returns `false` for `NaN`, positive infinity, or negative infinity.

---

### `isNaN`

Determines whether a value is `NaN` (Not a Number).

```js
isNaN(testValue);
```

---

### `parseInt` and `parseFloat`

Convert strings to numeric values.

```js
parseFloat(str);
parseInt(str, radix);
```

`parseInt` truncates the parsed value to an integer and supports different numeric bases.

---

### `Number` and `String`

Convert objects to numeric or string values.

```js
Number(objRef);
String(objRef);
```

---

### `escape` and `unescape`

Encode and decode strings using hexadecimal escape sequences.

```js
escape(string);
unescape(string);
```

Availability: not implemented in the current ProtoScript runtime. Calling
`escape(...)` or `unescape(...)` results in a `ReferenceError` because the
functions are undefined.
