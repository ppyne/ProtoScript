# ProtoScript — Buffer Module

**Specification v1.0**

This document defines the `Buffer` module for ProtoScript. The module provides a minimal, low-level binary memory abstraction suitable for image data, framebuffers, and binary I/O, without adopting JavaScript’s `ArrayBuffer` or typed array model.

The design is intentionally restrictive.

---

## 1. Scope and Goals

The Buffer module exists to:

- Represent raw binary data
- Allow explicit byte-level access
- Enable efficient manipulation of pixel buffers (e.g. RGBA 8-bit)
- Support binary file I/O
- Interoperate with native modules (e.g. Display)

Out of scope:

- High-level data structures
- Endianness abstraction
- Bitfields or packed views
- Automatic resizing
- Functional APIs (map, filter, etc.)

---

## 2. Design Principles

- Explicit memory ownership
- Fixed-size buffers
- Byte-addressable only
- No hidden conversions
- No implicit encoding
- Predictable GC behavior

The Buffer module models **raw memory**, not a collection.

---

## 3. Module Overview

The module is imported as:

```
import Buffer
```

A Buffer instance represents a contiguous region of memory.

---

## 4. API Reference

### 4.1 `Buffer.alloc(size)`

Allocates a new buffer of fixed size.

**Parameters:**
- `size` (number): size in bytes

**Returns:**
- a new Buffer instance

**Behavior:**
- memory is zero-initialized

**Errors:**
- throws if size is negative or exceeds implementation limits

---

### 4.2 `Buffer.size(buf)`

Returns the size of a buffer in bytes.

**Parameters:**
- `buf` (Buffer)

**Returns:**
- `number`

---

### 4.3 Byte Access

Buffers support indexed access using numeric indices:

```
value = buf[i]
buf[i] = value
```

**Rules:**

- `i` must be an integer
- valid range: `0 <= i < Buffer.size(buf)`
- values are clamped to `0–255`
- out-of-range access throws an error

---

### 4.4 `Buffer.slice(buf, offset, length)` (optional)

Creates a new Buffer containing a copy of a sub-range.

**Parameters:**
- `offset` (number)
- `length` (number)

**Returns:**
- a new Buffer

This is a copy, not a view.

---

## 5. Semantics

- Buffers are mutable
- Buffers have no prototype methods
- Buffers are opaque to user code
- Buffers are GC-managed objects

No assumptions are made about internal representation.

---

## 6. Error Handling

- Invalid indices throw synchronously
- Type mismatches throw synchronously
- No silent truncation except byte clamping

---

## 7. Performance Considerations

- Intended for tight loops
- No bounds elision
- No vectorization guarantees

Performance characteristics are implementation-defined.

---

## 8. Interoperability

The Buffer module is designed to interoperate with:

- `Io.readBinary` / `Io.writeBinary` (future extension)
- `Display` framebuffer or blitting APIs
- Other native modules requiring binary data

---

## 9. Explicit Non-Goals

The Buffer module will not:

- Provide multi-byte integer views
- Support floating-point storage
- Implement slicing views
- Expose pointers or addresses
- Provide iteration protocols

---

## 10. Philosophy

The Buffer module answers one question:

> “How do I manipulate raw bytes safely and explicitly?”

Nothing more.

