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
- assigned values are converted with `ToNumber` semantics
- `NaN`/`Infinity`/`-Infinity` become `0`
- values `<= 0` become `0`, values `>= 255` become `255`
- values in `(0, 1)` become `0`
- remaining values are rounded to nearest integer (`floor(x + 0.5)`)
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

### 4.5 `Buffer32.alloc(length)`

Allocates a new 32-bit buffer view backed by a byte buffer.

**Parameters:**
- `length` (number): number of 32-bit elements

**Returns:**
- a new Buffer32 instance

**Behavior:**
- memory is zero-initialized

**Errors:**
- throws if length is negative or exceeds implementation limits

---

### 4.6 `Buffer32.size(buf32)`

Returns the number of 32-bit elements.

**Parameters:**
- `buf32` (Buffer32)

**Returns:**
- `number`

---

### 4.7 `Buffer32.byteLength(buf32)`

Returns the size in bytes.

**Parameters:**
- `buf32` (Buffer32)

**Returns:**
- `number`

---

### 4.8 `Buffer32.view(buffer, offset?, length?)`

Creates a 32-bit view over an existing byte buffer.

**Parameters:**
- `buffer` (Buffer)
- `offset` (number, optional): element offset (uint32 index)
- `length` (number, optional): number of elements

**Returns:**
- a Buffer32 view (no copy)

**Errors:**
- throws if the view would exceed the buffer bounds

---

### 4.9 Buffer32 Access

Buffer32 supports indexed access using numeric indices:

```
value = buf32[i]
buf32[i] = value
```

**Rules:**

- `i` must be an integer
- valid range: `0 <= i < Buffer32.size(buf32)`
- elements are 32-bit unsigned integers (little-endian)
- assigned values are converted with `ToNumber` semantics
- `NaN`/`Infinity`/`-Infinity` become `0`
- values `<= 0` become `0`, values `>= 4294967295` become `4294967295`
- values in `(0, 1)` become `0`
- remaining values are rounded to nearest integer (`floor(x + 0.5)`)
- out-of-range access throws an error

---

## 5. Semantics

- Buffers are mutable
- Buffers have no prototype methods
- Buffers are opaque to user code
- Buffers are GC-managed objects

Buffer32 is a view that keeps its underlying Buffer alive.

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

- `Io.open(..., "rb"/"wb"/"ab")` and `file.read` / `file.write`
- `Display` framebuffer or blitting APIs
- Other native modules requiring binary data

---

## 9. Explicit Non-Goals

The Buffer module will not:

- Provide arbitrary typed array views beyond Buffer32
- Support floating-point storage
- Implement slicing views
- Expose pointers or addresses
- Provide iteration protocols

---

## 10. Philosophy

The Buffer module answers one question:

> “How do I manipulate raw bytes safely and explicitly?”

Nothing more.
