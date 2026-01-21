# Chapter 3 — Expressions and Operators

This chapter describes **ProtoScript expressions and operators**, including assignment, comparison, arithmetic, bitwise, logical, string, and special operators.

---

## Expressions

An **expression** is any valid combination of literals, variables, operators, and sub‑expressions that evaluates to a single value. That value may be a number, a string, or a Boolean value.

There are two conceptual categories of expressions:

- **Assignment expressions**, which assign a value to a variable and themselves evaluate to that value.
- **Value expressions**, which simply evaluate to a value without performing an assignment.

Examples:

```js
x = 7      // assignment expression, evaluates to 7
3 + 4      // value expression, evaluates to 7
```

ProtoScript expressions fall into three broad types:

- **Arithmetic expressions** — evaluate to numbers
- **String expressions** — evaluate to strings
- **Logical expressions** — evaluate to `true` or `false`

---

## Operators

ProtoScript provides **unary**, **binary**, and **ternary** operators.

- A **binary operator** requires two operands:

```js
operand1 operator operand2
```

- A **unary operator** requires one operand:

```js
operator operand
operand operator
```

- A **ternary operator** requires three operands (the conditional operator).

---

## Assignment Operators

The basic assignment operator is `=`:

```js
x = y
```

Additional assignment operators provide shorthand forms:

| Operator | Equivalent |
|---------|------------|
| `+=` | `x = x + y` |
| `-=` | `x = x - y` |
| `*=` | `x = x * y` |
| `/=` | `x = x / y` |
| `%=` | `x = x % y` |
| `<<=` | `x = x << y` |
| `>>=` | `x = x >> y` |
| `>>>=` | `x = x >>> y` |
| `&=` | `x = x & y` |
| `^=` | `x = x ^ y` |
| `|=` | `x = x | y` |

---

## Comparison Operators

Comparison operators compare operands and return a Boolean value.

| Operator | Description |
|---------|-------------|
| `==` | Equal (type conversion allowed) |
| `!=` | Not equal |
| `===` | Strict equal (no type conversion) |
| `!==` | Strict not equal |
| `>` | Greater than |
| `>=` | Greater than or equal |
| `<` | Less than |
| `<=` | Less than or equal |

Strings are compared lexicographically using Unicode code points.

---

## Arithmetic Operators

Arithmetic operators operate on numeric values and return numeric results.

Standard operators:

```js
+  -  *  /
```

Additional arithmetic operators:

| Operator | Description |
|---------|-------------|
| `%` | Remainder |
| `++` | Increment |
| `--` | Decrement |
| `-` | Unary negation |

Division always produces a floating‑point result:

```js
1 / 2   // 0.5
```

---

## Bitwise Operators

Bitwise operators treat operands as **32‑bit signed integers**.

| Operator | Description |
|---------|-------------|
| `&` | Bitwise AND |
| `|` | Bitwise OR |
| `^` | Bitwise XOR |
| `~` | Bitwise NOT |
| `<<` | Left shift |
| `>>` | Sign‑propagating right shift |
| `>>>` | Zero‑fill right shift |

Example:

```js
15 & 9   // 9
15 | 9   // 15
15 ^ 9   // 6
```

---

## Logical Operators

Logical operators work with Boolean values but may return non‑Boolean operands.

| Operator | Description |
|---------|-------------|
| `&&` | Logical AND |
| `||` | Logical OR |
| `!` | Logical NOT |

Examples:

```js
"Cat" && "Dog"   // "Dog"
false || "Cat"  // "Cat"
```

### Short‑Circuit Evaluation

Logical expressions are evaluated left‑to‑right:

- `false && anything` → `false`
- `true || anything` → `true`

The second operand may not be evaluated.

---

## String Operators

The `+` operator concatenates strings:

```js
"my " + "string"   // "my string"
```

The `+=` operator may also be used for concatenation.

---

## Special Operators

### Conditional Operator

```js
condition ? value1 : value2
```

Example:

```js
status = (age >= 18) ? "adult" : "minor";
```

---

### Comma Operator

Evaluates multiple expressions and returns the last one. Commonly used in `for` loops:

```js
for (i = 0, j = 9; i <= 9; i++, j--) {
    // loop body
}
```

---

### `delete`

Removes a property or object reference:

```js
delete obj.property
delete obj[index]
```

Deleting an array element does not change the array length.

---

### `new`

Creates a new object instance:

```js
obj = new ObjectType(args);
```

---

### `this`

Refers to the current object within a method:

```js
this.propertyName
```

---

### `typeof`

Returns a string indicating the operand’s type:

```js
typeof 42        // "number"
typeof "hello"   // "string"
typeof null      // "object"
```

---

### `void`

Evaluates an expression and returns `undefined`:

```js
void(expression)
```

---

## Operator Precedence

Operators are applied according to precedence rules. Parentheses can be used to override precedence.

From lowest to highest precedence:

1. comma
2. assignment
3. conditional
4. logical OR
5. logical AND
6. bitwise OR
7. bitwise XOR
8. bitwise AND
9. equality
10. relational
11. shift
12. addition / subtraction
13. multiplication / division / remainder
14. unary operators
15. function call
16. `new`
17. member access (`.` `[]`)

