# ProtoScript — Io Binary Extension

**Status:** Superseded by `docs/specs/io_unified_spec.md` (v1.2). This document is
kept for historical reference only.

**Specification v1.0**

This document defines the binary I/O extensions to the existing `Io` module. These extensions introduce explicit binary file access using the `Buffer` type, without altering or overloading existing text-based APIs.

---

## 1. Scope and Rationale

The original `Io` module is intentionally text-oriented:

- `Io.read` reads UTF-8 text
- `Io.write` writes UTF-8 text
- binary data is explicitly rejected

This extension adds **separate, explicit APIs** for binary I/O, avoiding ambiguity between text and binary data.

---

## 2. Design Principles

- Explicit is better than implicit
- Text and binary I/O are strictly separated
- No encoding or decoding is performed
- No automatic conversions
- Symmetric read / write behavior

Binary I/O must never interfere with text semantics.

---

## 3. Module Overview

The binary functions are part of the existing `Io` module:

```
import Io
```

They require the `Buffer` module.

---

## 4. API Reference

### 4.1 `Io.readBinary(path)`

Reads the entire contents of a file as raw binary data.

**Parameters:**
- `path` (string): filesystem path

**Returns:**
- `Buffer`

**Behavior:**
- opens the file in binary mode
- reads the full file into memory
- returns a Buffer of exact file size

**Errors:**
- throws if the file cannot be opened
- throws if the file cannot be read

No character encoding, BOM handling, or validation is performed.

---

### 4.2 `Io.writeBinary(path, buffer)`

Writes raw binary data to a file.

**Parameters:**
- `path` (string): filesystem path
- `buffer` (Buffer): data to write

**Behavior:**
- opens or creates the file in binary mode
- truncates existing file
- writes the entire buffer

**Errors:**
- throws if the file cannot be opened
- throws if the file cannot be written

---

## 5. Semantics

- Binary reads and writes are atomic at the API level
- Partial reads or writes are not exposed
- File size is determined before allocation
- No streaming or incremental I/O is provided (v1)

---

## 6. Interaction with Text APIs

- `Io.read` and `Io.write` remain unchanged
- Binary functions do not accept or return strings
- Text functions must not accept Buffer arguments

Any misuse results in a synchronous error.

---

## 7. Error Handling

- All errors are synchronous and fatal
- No silent truncation
- No implicit retries

---

## 8. Security and Predictability

- No automatic path normalization beyond platform defaults
- No sandboxing implied
- Caller is responsible for file size limits

---

## 9. Explicit Non-Goals

This extension does not provide:

- streaming APIs
- partial reads
- memory-mapped files
- encoding-aware binary I/O
- mixed text/binary modes

---

## 10. Philosophy

The binary Io extension answers one question:

> “How do I read and write raw bytes, without ambiguity?”

Nothing more.
