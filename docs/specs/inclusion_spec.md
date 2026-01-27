# ProtoScript — Source Inclusion (`ProtoScript.include`)

**Specification v1.1**

This document defines the `ProtoScript.include` directive for ProtoScript. The purpose of `ProtoScript.include` is to allow **explicit, static reuse of source code across multiple projects**, without introducing a full module system or dynamic code loading.

---

## 1. Scope and Intent

The `ProtoScript.include` directive exists to:

- Reuse ProtoScript source files across projects
- Avoid code duplication
- Keep execution deterministic
- Preserve ProtoScript’s single-entry-point execution model

`ProtoScript.include` is **not** a module system and does not introduce encapsulation, namespaces, or exports.

---

## 2. Syntax

```js
ProtoScript.include("relative/or/absolute/path.js");
```

Rules:

- The argument must be a string literal
- No expressions or variables are allowed
- The file extension must be `.js`
- The call must be written exactly as `ProtoScript.include(...)`
- The statement must end with a semicolon
- This is a parse-time directive, not a runtime function

---

## 3. Semantics

### 3.0 Placement Rules

- `ProtoScript.include` statements may appear **only at top level** of a source file
- `ProtoScript.include` is **not allowed** inside functions, loops, conditionals, or any other block
- `ProtoScript.include` must appear **before any executable statements** in the file

This rule guarantees:
- a fully static source tree
- deterministic execution order
- absence of runtime-dependent inclusion

Any violation of these rules results in a **syntax error**.

---

## 3. Semantics

### 3.1 Inclusion Model

- `ProtoScript.include` performs a **static textual inclusion**
- Included files are parsed and executed **as if their contents were written inline**
- All included code executes in the **same global scope** as the including file

Conceptually:

```js
ProtoScript.include("a.js");
ProtoScript.include("b.js");
```

is equivalent to:

```js
// contents of a.js
// contents of b.js
```

followed by the rest of the source file.

---

### 3.2 Execution Order

- Files are included **in source order**, top to bottom
- Each file is included and executed **exactly once per `ProtoScript.include` statement**
- There is no implicit caching or deduplication

---

## 4. Path Resolution Rules

### 4.1 Relative Paths

- Relative paths are resolved **relative to the file that contains the `ProtoScript.include` statement**, not the process working directory

Example:

```js
// main.js
ProtoScript.include("ui/system7/draw.js");
```

If `main.js` is located in `/project/`, the resolved path is:

```
/project/ui/system7/draw.js
```

---

### 4.2 Absolute Paths

- Absolute paths are resolved according to platform rules
- No path normalization beyond basic resolution is performed

---

### 4.3 Path Validity

- The target file must exist
- The target file must be readable
- The target file must be a regular file

Failure in any of these cases results in a fatal error.

---

## 5. Cycles and Multiple Inclusion

### 5.1 Cyclic Includes

Cyclic includes are **explicitly forbidden**.

Example (invalid):

```js
// a.js
ProtoScript.include("b.js");

// b.js
ProtoScript.include("a.js");
```

Behavior:

- The runtime detects include cycles
- Execution stops with a fatal error
- The error reports the include chain

This rule avoids:

- accidental infinite recursion
- duplicated definitions
- unpredictable execution order

---

### 5.2 Multiple Includes of the Same File

- Including the same file multiple times from different locations is **allowed**, provided it does not form a cycle
- Each inclusion results in a fresh execution of the file contents

Deduplication, guards, or `#pragma once`-style behavior are **explicitly out of scope** for v1.

---

## 6. Error Handling

Errors raised during inclusion are fatal:

- file not found
- read error
- syntax error in included file
- include cycle detected

Errors abort program execution before entering the main execution phase.

---

## 7. Security and Determinism

- `ProtoScript.include` does not execute arbitrary strings
- No runtime code generation occurs
- Included code is subject to the same parsing and validation as the main file
- Behavior is fully deterministic

---

## 8. Relationship to `eval`

- `ProtoScript.include` is **not** `eval`
- `ProtoScript.include` is resolved **before execution**, not at runtime
- `eval` remains disabled by default and is orthogonal to `ProtoScript.include`

---

## 9. Explicit Non-Goals

The `ProtoScript.include` directive does **not** provide:

- dynamic loading
- conditional inclusion
- namespaces or modules
- symbol isolation
- package management

These may be considered in future, separate specifications.

---

## 10. Philosophy

The `ProtoScript.include` directive answers one question:

> “How can I reuse code explicitly, safely, and predictably?”

Nothing more.
