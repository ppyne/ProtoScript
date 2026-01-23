# ProtoScript — Source Inclusion (`include`)

**Specification v1.1**

This document defines the `include` directive for ProtoScript. The purpose of `include` is to allow **explicit, static reuse of source code across multiple projects**, without introducing a full module system or dynamic code loading.

---

## 1. Scope and Intent

The `include` directive exists to:

- Reuse ProtoScript source files across projects
- Avoid code duplication
- Keep execution deterministic
- Preserve ProtoScript’s single-entry-point execution model

`include` is **not** a module system and does not introduce encapsulation, namespaces, or exports.

---

## 2. Syntax

```js
include "relative/or/absolute/path.js"
```

Rules:

- The argument must be a string literal
- No expressions or variables are allowed
- The file extension must be `.js`

---

## 3. Semantics

### 3.0 Placement Rules

- `include` statements may appear **only at top level** of a source file
- `include` is **not allowed** inside functions, loops, conditionals, or any other block
- `include` must appear **before any executable statements** in the file

This rule guarantees:
- a fully static source tree
- deterministic execution order
- absence of runtime-dependent inclusion

Any violation of these rules results in a **syntax error**.

---

## 3. Semantics

### 3.1 Inclusion Model

- `include` performs a **static textual inclusion**
- Included files are parsed and executed **as if their contents were written inline**
- All included code executes in the **same global scope** as the including file

Conceptually:

```js
include "a.js"
include "b.js"
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
- Each file is included and executed **exactly once per **``** statement**
- There is no implicit caching or deduplication

---

## 4. Path Resolution Rules

### 4.1 Relative Paths

- Relative paths are resolved **relative to the file that contains the **``** statement**, not the process working directory

Example:

```js
// main.js
include "ui/system7/draw.js"
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
include "b.js"

// b.js
include "a.js"
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

- `include` does not execute arbitrary strings
- No runtime code generation occurs
- Included code is subject to the same parsing and validation as the main file
- Behavior is fully deterministic

---

## 8. Relationship to `eval`

- `include` is **not** `eval`
- `include` is resolved **before execution**, not at runtime
- `eval` remains disabled by default and is orthogonal to `include`

---

## 9. Explicit Non-Goals

The `include` directive does **not** provide:

- dynamic loading
- conditional inclusion
- namespaces or modules
- symbol isolation
- package management

These may be considered in future, separate specifications.

---

## 10. Philosophy

The `include` directive answers one question:

> “How can I reuse code explicitly, safely, and predictably?”

Nothing more.

