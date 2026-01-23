![ProtoScript](../../header.png)

# ProtoScript Execution Model

ProtoScript is a **standalone scripting language** designed to be executed from the command line.

ProtoScript is a C reimplementation of an ECMAScript 1 core (almost JavaScript 1.3), designed as a minimal, embeddable scripting language.

It is a standalone command-line interpreter and does not target browsers, the DOM, HTML, or any client-side runtime.

The engine uses a glyph-based Unicode model (UTF-8), not UTF-16.

ProtoScript does not aim for modern JavaScript or browser compatibility.

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
echo 'Io.print("Hello world\n");'| ./protoscript
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

ProtoScript also exposes a global `ProtoScript` object with runtime metadata:

- `ProtoScript.args`: readonly array of command-line arguments (`argv`)
- `ProtoScript.version`: `"v1.0.0 ECMAScript 262 (ES1)"`

Example:

```js
Io.print(ProtoScript.version + "\n");
Io.print(ProtoScript.args.length + "\n");
```

---

## Input and Output

ProtoScript does not define built-in user interface primitives. Any input/output facilities (such as `print`, file access, or standard input) are provided by the **ProtoScript runtime**, not by the language specification itself.

Language examples in this documentation focus strictly on **language semantics**, not on I/O mechanisms.

---

## Garbage Collection

ProtoScript uses a **precise, stop-the-world mark-and-sweep GC** for objects,
strings, functions, and environments. Collections run automatically when the
heap crosses adaptive thresholds, and can also be triggered manually via the
`Gc` module.

```js
Gc.collect();
var stats = Gc.stats();
Io.print(stats.totalBytes + "\n");
```

Notes:
- GC never closes external resources; file handles must be closed explicitly.
- `Gc.stats()` returns counters for heap usage and collection activity.

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
