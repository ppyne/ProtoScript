# ProtoScript – Io Module Unified Specification

**Specification version: 1.2**

## Scope

This document **replaces and supersedes**:

- `io_spec.md`
- `io_binary_extension_spec.md`

All I/O functionality is defined here. There is **no separate binary extension**.

The design goals are:

- Single, coherent API
- Explicit control over text vs binary
- Predictable, low‑level behavior
- Sequential reading support
- ES1 / ProtoScript minimalism

---

## Design Principles

- **One open function** for text and binary
- **Mode flags**, not separate APIs
- **No implicit conversions**
- **Deterministic resource lifetime** (explicit close)
- **Sequential access is first‑class**
- UTF‑8 is the only supported text encoding

---

## File Handle

`Io.open(path, mode)` returns a **File object**.

A File object represents an open file descriptor with an internal cursor.

### Properties

- `file.path` : string (original path)
- `file.mode` : string (mode flags)
- `file.closed` : boolean

---

## Io.open(path, mode)

### Mode flags

| Flag | Meaning                |
| ---- | ---------------------- |
| `r`  | read (text)            |
| `w`  | write (text, truncate) |
| `a`  | append (text)          |
| `b`  | binary mode            |

Flags can be combined:

- `rb` → read binary
- `wb` → write binary
- `ab` → append binary

Text mode is default when `b` is absent.

### Errors

- File not found
- Permission denied
- Invalid mode string

---

## Standard Streams

The following standard streams are provided as already-open file handles:

- `Io.stdin`
- `Io.stdout`
- `Io.stderr`

They must not be closed by user code. Calling `file.close()` on any of them
must raise an error.

---

## Io.EOL

`Io.EOL` is the end-of-line string:

```
Io.EOL == "\n"
```

---

## Io.print(value)

Writes `value` to `Io.stdout` without adding a newline. It is equivalent to:

```
Io.stdout.write(String(value))
```

---

## Io.tempPath()

Returns a unique, non-existing temporary file path as a string. The file is
not created. Any failure must throw an exception.

---

## Io.EOF

`Io.EOF` is a unique constant used to signal end-of-file in sequential read operations.

- It is **not** a string, number, or Buffer
- It must be compared by **identity** only
- It is returned only by `file.read(size)` when EOF is reached

---

## file.read([size])

Reads data from the file starting at the **current cursor position**.

### Signatures

```
file.read()
file.read(size)
```

### Behavior

#### Common rules

- Reading starts at the current cursor position
- The cursor advances by the number of bytes actually read
- EOF is **not** an error

#### `file.read()` (no size)

- Reads until EOF
- Returns:
  - text mode: `string`
  - binary mode: `Buffer`
- Never returns `Io.EOF`

#### `file.read(size)`

- Attempts to read **up to** `size` bytes
- May return **fewer than ****\`\`**** bytes** if EOF is encountered

Return values:

| Situation                                    | Return value                        |
| -------------------------------------------- | ----------------------------------- |
| `size` bytes available                       | Buffer / string of length `size`    |
| fewer than `size` bytes available            | Buffer / string of remaining length |
| zero bytes available (cursor already at EOF) | `Io.EOF`                            |

### EOF semantics (normative)

`Io.EOF` is returned **if and only if**:

- `file.read(size)` is called
- and **zero bytes** are read
- because the cursor is already at end-of-file

`Io.EOF` MUST NOT be returned if at least one byte is read.



---

## file.write(data)

Writes data at the current cursor position.

### Accepted types

| Mode   | Accepted |
| ------ | -------- |
| text   | string   |
| binary | Buffer   |

### Type rules (normative)

- In **text mode**, `data` MUST be a string
- In **binary mode**, `data` MUST be a Buffer
- Passing a string to `file.write` in binary mode MUST throw a type error
- Passing a Buffer to `file.write` in text mode MUST throw a type error

No implicit conversion between string and Buffer is ever performed.

### Behavior

- Cursor advances after write
- No implicit newline is added

---

## file.close()

Closes the file.

- Idempotent
- After close, any operation raises an error

---

## Binary Data

### Buffer type

Binary reads and writes use a `Buffer` object.

Minimal required interface:

- `buffer.length`
- `buffer[i]` → integer 0–255

Buffer creation is implementation‑defined (out of scope of Io).

---

## UTF‑8 Rules (Text Mode)

- Input is decoded as UTF‑8
- **Only a UTF‑8 BOM (U+FEFF) at the very beginning of the file is accepted and ignored**
- A BOM appearing at any other position **MUST raise an exception**
- Any other BOM or byte-order mark sequence **MUST raise an exception**
- Invalid UTF‑8 sequences **MUST raise an exception**
- `�` (NUL) **is forbidden** in text mode and **MUST raise an exception**

Binary mode (`b`) allows any byte value.

---

## Sequential Reading Pattern (Example)

```js
var f = Io.open("data.bin", "rb");

while (true) {
  var chunk = f.read(1024);
  if (chunk === Io.EOF) break;
  process(chunk);
}

f.close();
```

---

## Explicit Non‑Goals

- No implicit buffering policy exposed
- No encoding selection
- No automatic file closing (GC does not close files)
- No partial UTF‑8 codepoint recovery

---

## Rationale

- `readBinary` / `writeBinary` removed → \*\*mode flag \*\*\`\` is simpler and more orthogonal
- `read(size)` enables streaming, parsers, and large file handling
- Single spec reduces ambiguity and maintenance cost

---

## Tests and Documentation Requirements

- All existing tests MUST be updated to comply with this specification (v1.2)
- Any test relying on deprecated APIs or behaviors MUST be rewritten or removed
- New tests MUST cover:
  - text vs binary mode separation
  - `Io.EOF` semantics
  - partial reads and EOF behavior
  - strict type errors (string vs Buffer)
  - UTF-8 BOM handling and rejection cases

All user-facing documentation MUST be updated accordingly.

### User Guide Requirements

The user guide MUST include practical, runnable examples demonstrating:

- Text (ASCII / UTF-8) file reading and writing
- Binary file reading and writing using `Buffer`
- Sequential reading with `read(size)` and `Io.EOF`
- Correct error cases (invalid mode, type mismatch, invalid BOM)

Examples MUST clearly distinguish text and binary workflows and MUST NOT rely on implicit conversions.

---

## Summary

- One spec
- One open
- Mode flags define semantics
- Sequential I/O is explicit and predictable

This specification is normative.
