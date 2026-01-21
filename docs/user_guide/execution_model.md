# ProtoScript Execution Model

ProtoScript is a **standalone scripting language** designed to be executed from the command line.

---

## Running a ProtoScript Program

A ProtoScript program is a plain text file containing ProtoScript source code, typically using the `.js` extension.

To execute a script, invoke the ProtoScript interpreter directly from the terminal:

```sh
./protoscript script.js
```

The interpreter parses the file, evaluates the code from top to bottom, and terminates when execution completes.

Usage (file or stdin):

```sh
./protoscript script.js
cat script.js | ./protoscript
echo 'Io.print("Hello world");'| ./protoscript
./protoscript < script.js
./protoscript - < script.js
```

---

## Execution Environment

ProtoScript provides a **single global execution context**:

- No browser environment
- No DOM or HTML objects
- No window, document, navigator, or events
- No asynchronous execution model
- No implicit user interaction

All code executes synchronously in a single thread.

---

## Global Scope

All top-level declarations are evaluated in the **global scope**:

```js
var x = 10;
function square(n) {
    return n * n;
}
```

Top-level `var` and function declarations are hoisted, so they are visible
throughout the script even before the declaration is reached (with the usual
JavaScript 1.x initialization rules).

---

## Input and Output

ProtoScript does not define built-in user interface primitives. Any input/output facilities (such as `print`, file access, or standard input) are provided by the **ProtoScript runtime**, not by the language specification itself.

Language examples in this documentation focus strictly on **language semantics**, not on I/O mechanisms.

---

## Program Termination

A ProtoScript program terminates when:

- the end of the script is reached, or
- execution exits the current control flow naturally

There is no event loop and no background execution after the script finishes.

---

## Compatibility Scope

This documentation describes only:

- JavaScript 1.3 core language features
- deterministic, synchronous execution
- prototype-based object model

It intentionally excludes:

- browser-specific APIs
- HTML integration
- DOM manipulation
- LiveConnect and Java integration
- client-side security and events
