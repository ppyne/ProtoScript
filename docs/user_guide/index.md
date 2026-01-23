![ProtoScript](../../header.png)

# Index

This index lists core **ProtoScript language concepts**, operators, statements, objects, and functions.

---

## Symbols

- `!` — logical NOT operator (see [Expressions and Operators](expressions_and_operators.md))
- `!=`, `!==` — comparison operators (see [Expressions and Operators](expressions_and_operators.md#comparison-operators))
- `%`, `%=` — remainder operator (see [Expressions and Operators](expressions_and_operators.md#arithmetic-operators))
- `&`, `&&` — bitwise AND, logical AND (see [Expressions and Operators](expressions_and_operators.md#bitwise-operators) / [Expressions and Operators](expressions_and_operators.md#logical-operators))
- `|`, `||` — bitwise OR, logical OR (see [Expressions and Operators](expressions_and_operators.md#bitwise-operators) / [Expressions and Operators](expressions_and_operators.md#logical-operators))
- `^` — bitwise XOR (see [Expressions and Operators](expressions_and_operators.md#bitwise-operators))
- `~` — bitwise NOT (see [Expressions and Operators](expressions_and_operators.md#bitwise-operators))
- `+`, `+=` — addition, string concatenation (see [Expressions and Operators](expressions_and_operators.md#arithmetic-operators) / [Expressions and Operators](expressions_and_operators.md#string-operators))
- `-`, `--` — unary negation, decrement (see [Expressions and Operators](expressions_and_operators.md#arithmetic-operators))
- `*`, `*=` — multiplication (see [Expressions and Operators](expressions_and_operators.md#arithmetic-operators))
- `/`, `/=` — division (see [Expressions and Operators](expressions_and_operators.md#arithmetic-operators))
- `<<`, `>>`, `>>>` — bitwise shift operators (see [Expressions and Operators](expressions_and_operators.md#bitwise-operators))
- `?:` — conditional operator (see [Expressions and Operators](expressions_and_operators.md#special-operators))
- `,` — comma operator (see [Expressions and Operators](expressions_and_operators.md#special-operators))

---

## A

- `arguments` array — function arguments (see [Functions](functions.md#using-the-arguments-array))
- arithmetic operators (see [Expressions and Operators](expressions_and_operators.md#arithmetic-operators))
- `Array` object (see [Working with Objects](working_with_objects.md#predefined-core-objects))
- arrays (see [Working with Objects](working_with_objects.md#predefined-core-objects))
  - associative arrays
  - indexing
  - iteration

---

## B

- bitwise operators (see [Expressions and Operators](expressions_and_operators.md#bitwise-operators))
- `Boolean` object (see [Working with Objects](working_with_objects.md#predefined-core-objects))
- Boolean literals (see [Values, Variables, and Literals](values_variables_and_literals.md#literals))
- `break` statement (see [Statements](statements.md#loop-statements))
- `Buffer` module (see [Working with Objects](working_with_objects.md#buffer-module))
  - `Buffer.alloc`
  - `Buffer.size`
  - `Buffer.slice`

---

## C

- comments (`//`, `/* */`) (see [Statements](statements.md#comments))
- comparison operators (see [Expressions and Operators](expressions_and_operators.md#comparison-operators))
- conditional expressions (see [Expressions and Operators](expressions_and_operators.md#special-operators))
- conditional statements (`if`, `switch`) (see [Statements](statements.md#conditional-statements))
- constructors (see [Object Model and Prototypes](object_model_and_prototypes.md#constructor-functions-and-inheritance))
- `continue` statement (see [Statements](statements.md#loop-statements))

---

## D

- data types (see [Values, Variables, and Literals](values_variables_and_literals.md#values))
- `Date` object (see [Working with Objects](working_with_objects.md#predefined-core-objects))
- `delete` operator (see [Expressions and Operators](expressions_and_operators.md#special-operators))
- `do...while` statement (see [Statements](statements.md#loop-statements))
- `Display` module (see [Working with Objects](working_with_objects.md#display-module))
  - `Display.open`
  - `Display.close`
  - `Display.size`
  - `Display.clear`
  - `Display.pixel`
  - `Display.line`
  - `Display.rect`
  - `Display.fillRect`
  - `Display.framebuffer`
  - `Display.blitRGBA`
  - `Display.present`

---

## E

- `else` clause (see [Statements](statements.md#conditional-statements))
- `eval` function (see [Functions](functions.md#predefined-functions))
- expressions (see [Expressions and Operators](expressions_and_operators.md#expressions))
- `Error` object (see [Language Reference](../language_reference.md))
- `Event` module (see [Working with Objects](working_with_objects.md#event-module))
  - `Event.next`
- `EvalError` object (see [Language Reference](../language_reference.md))

---

## F

- `for` statement (see [Statements](statements.md#loop-statements))
- `for...in` statement (see [Statements](statements.md#object-manipulation-statements))
- functions (see [Functions](functions.md#defining-functions))
  - defining
  - calling
  - recursive functions
- `Fs` module (see [Working with Objects](working_with_objects.md#fs-module))
  - `Fs.chmod`
  - `Fs.cp`
  - `Fs.exists`
  - `Fs.size`
  - `Fs.isDir`
  - `Fs.isFile`
  - `Fs.isSymlink`
  - `Fs.isExecutable`
  - `Fs.isReadable`
  - `Fs.isWritable`
  - `Fs.ls`
  - `Fs.mkdir`
  - `Fs.mv`
  - `Fs.pathInfo`
  - `Fs.pwd`
  - `Fs.cd`
  - `Fs.rmdir`
  - `Fs.rm`

---

## G

- global object (see [ProtoScript Execution Model](execution_model.md#global-scope))
- `Gc` module (see [Working with Objects](working_with_objects.md#gc-module))
  - `Gc.collect`
  - `Gc.stats`

---

## I

- `if...else` statement (see [Statements](statements.md#conditional-statements))
- inheritance (see [Object Model and Prototypes](object_model_and_prototypes.md#creating-an-object-hierarchy))
- initialization (see [Values, Variables, and Literals](values_variables_and_literals.md#declaring-variables))
- `isFinite` function (see [Functions](functions.md#predefined-functions))
- `isNaN` function (see [Functions](functions.md#predefined-functions))
- `Image` module (see [Working with Objects](working_with_objects.md#image-module))
  - `Image.detectFormat`
  - `Image.decodePNG`
  - `Image.decodeJPEG`
  - `Image.resample`
- `Io` module (see [Working with Objects](working_with_objects.md#io-module))
  - `Io.print`
  - `Io.sprintf`
  - `Io.open`
  - `Io.tempPath`
  - `Io.EOL`
  - `Io.EOF`
  - `Io.stdin`
  - `Io.stdout`
  - `Io.stderr`
  - `console.log`
  - `console.info`
  - `console.warn`
  - `console.error`

---

## J

- `JSON` object (see [Language Reference](../language_reference.md))

---

## L

- labels (see [Statements](statements.md#loop-statements))
- logical operators (see [Expressions and Operators](expressions_and_operators.md#logical-operators))
- loops (see [Statements](statements.md#loop-statements))

---

## M

- `Math` object (see [Working with Objects](working_with_objects.md#predefined-core-objects))
- methods (see [Working with Objects](working_with_objects.md#defining-methods))

---

## N

- `new` operator (see [Expressions and Operators](expressions_and_operators.md#special-operators))
- `null` (see [Values, Variables, and Literals](values_variables_and_literals.md#values))
- `Number` object (see [Working with Objects](working_with_objects.md#predefined-core-objects))

---

## O

- object literals (see [Values, Variables, and Literals](values_variables_and_literals.md#object-literals))
- object model (see [Object Model and Prototypes](object_model_and_prototypes.md#object-properties-and-the-prototype-chain))
- objects (see [Working with Objects](working_with_objects.md#objects-and-properties))

---

## P

- parameters (see [Functions](functions.md#defining-functions))
- properties (see [Working with Objects](working_with_objects.md#objects-and-properties))
- prototypes (see [Object Model and Prototypes](object_model_and_prototypes.md#object-properties-and-the-prototype-chain))
- `ProtoScript` object (see [ProtoScript Execution Model](execution_model.md#global-scope))
  - `ProtoScript.args`
  - `ProtoScript.version`
  - `ProtoScript.exit`
  - `ProtoScript.sleep`
  - `ProtoScript.usleep`

---

## R

- `RegExp` object (see [Regular Expressions](regular_expressions.md#creating-a-regular-expression))
- regular expressions (see [Regular Expressions](regular_expressions.md#creating-a-regular-expression))
- `RangeError` object (see [Language Reference](../language_reference.md))
- `ReferenceError` object (see [Language Reference](../language_reference.md))
- `return` statement (see [Statements](statements.md))

---

## S

- `String` object (see [Working with Objects](working_with_objects.md#predefined-core-objects))
- string operators (see [Expressions and Operators](expressions_and_operators.md#string-operators))
- `switch` statement (see [Statements](statements.md#conditional-statements))
- `SyntaxError` object (see [Language Reference](../language_reference.md))

---

## T

- `this` keyword (see [Working with Objects](working_with_objects.md#using-this))
- `typeof` operator (see [Expressions and Operators](expressions_and_operators.md#special-operators))
- `TypeError` object (see [Language Reference](../language_reference.md))

---

## U

- `undefined` (see [Values, Variables, and Literals](values_variables_and_literals.md#values))

---

## V

- variables (see [Values, Variables, and Literals](values_variables_and_literals.md#variables))
- `void` operator (see [Expressions and Operators](expressions_and_operators.md#special-operators))

---

## W

- `while` statement (see [Statements](statements.md#loop-statements))
- `with` statement (see [Statements](statements.md#object-manipulation-statements))
