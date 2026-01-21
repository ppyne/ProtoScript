![ProtoScript](../../header.png)

# Chapter 5 — Statements

ProtoScript supports a compact set of **statements** used to control program flow, repeat operations, manipulate objects, and document code. This chapter provides an overview of all core statements.

Any expression is also a statement. Statements are typically separated by semicolons (`;`).

---

## Conditional Statements

Conditional statements execute code based on the evaluation of a condition.

### `if...else`

Executes a block of code if a condition is true, and optionally executes another block if the condition is false.

```js
if (condition) {
    statements1;
} else {
    statements2;
}
```

The condition may be any expression that evaluates to a Boolean value. Objects (even `new Boolean(false)`) evaluate to `true` unless they are `null` or `undefined`.

Example:

```js
function checkLength(value) {
    if (value.length == 3) {
        return true;
    } else {
        return false;
    }
}
```

---

### `switch`

Evaluates an expression and matches its value against case labels.

```js
switch (expression) {
    case value1:
        statements;
        break;
    case value2:
        statements;
        break;
    default:
        statements;
}
```

If `break` is omitted, execution continues into the next case (fall-through behavior).

---

## Loop Statements

Loop statements execute code repeatedly while a condition remains true.

### `for`

```js
for (initialExpression; condition; incrementExpression) {
    statements;
}
```

Execution order:
1. Evaluate the initialization expression
2. Test the condition
3. Execute the loop body
4. Execute the increment expression
5. Repeat from step 2

---

### `do...while`

```js
do {
    statements;
} while (condition);
```

The loop body is executed at least once before the condition is tested.

---

### `while`

```js
while (condition) {
    statements;
}
```

The condition is evaluated before each iteration.

Example:

```js
n = 0;
x = 0;
while (n < 3) {
    n++;
    x += n;
}
```

---

### `label`

A label assigns an identifier to a statement. Labels are commonly used with `break` and `continue`.

```js
labelName:
    statement;
```

Example:

```js
outerLoop:
while (condition) {
    doSomething();
}
```

---

### `break`

Terminates a loop, `switch`, or labeled statement.

```js
break;
break labelName;
```

---

### `continue`

Restarts the next iteration of a loop or labeled loop.

```js
continue;
continue labelName;
```

---

## Object Manipulation Statements

### `for...in`

Iterates over the enumerable properties of an object.

```js
for (var property in object) {
    statements;
}
```

Example:

```js
function dumpProps(obj) {
    for (var p in obj) {
        Io.print(p + " = " + obj[p] + "\n");
    }
}
```

---

### `with`

Temporarily sets an object as the default scope for property lookup.

```js
with (Math) {
    a = PI * r * r;
    x = r * cos(PI);
    y = r * sin(PI / 2);
}
```

Note: although supported in ProtoScript for compatibility, `with` is generally discouraged because it makes code harder to read and optimize.
Availability: compile-time gated by `PS_ENABLE_WITH` (default `0` in
`include/ps_config.h`). When disabled, parsing a `with` statement fails with
`Parse error: 'with' is disabled`.

---

## Comments

Comments are ignored by the interpreter and are used to document code.

### Single-line comments

```js
// This is a single-line comment
```

### Multi-line comments

```js
/*
   This is a multi-line comment.
*/
```
