![ProtoScript](../header.png)

# ProtoScript — Internal String Specification

## 1. Definition
A `String` in ProtoScript is an **immutable finite sequence of Unicode glyphs**.

Internally, strings are stored using **UTF-8 encoding**, but all observable semantics are defined in terms of **glyphs**, not bytes.

UTF-8 is considered an implementation detail.

---

## 2. Memory Representation

```c
typedef struct PSString {
    char     *utf8;          /* UTF-8 buffer, not null-terminated */
    size_t    byte_len;      /* length in bytes */
    uint32_t *glyph_offsets; /* byte offsets of each glyph */
    size_t    glyph_count;   /* number of glyphs */
} PSString;
```

### Invariants
- `utf8` always contains a **valid UTF-8 sequence**
- `glyph_offsets[i]` points to the **first byte** of the i-th glyph
- `glyph_offsets` is strictly increasing
- `glyph_count` is fixed at creation time
- Strings are **immutable** after creation

The empty string has:
- `byte_len = 0`
- `glyph_count = 0`
- `glyph_offsets = NULL`

---

## 3. String Creation

When a string is created:
1. The UTF-8 input is validated
2. Glyph boundaries are detected
3. `glyph_offsets` is built
4. `glyph_count` is finalized

All strings are therefore **normalized and validated at creation time**.

---

## 4. Observable Semantics

### 4.1 Length

```js
string.length
```

Returns the **number of Unicode glyphs** contained in the string.

Formally:
```
string.length === glyph_count
```

---

### 4.2 Glyph Indexing

All string indexing operations are **glyph-based** and **zero-based**.

Valid indices are:
```
0 <= index < string.length
```

---

### 4.3 charAt(index)

```js
string.charAt(index)
```

Behavior:
- If `index` is out of bounds → returns the empty string `""`
- Otherwise → returns a new `String` containing **exactly one glyph**

The glyph corresponds to the UTF-8 byte range:
```
utf8[glyph_offsets[index] .. glyph_offsets[index + 1] - 1]
```

---

### 4.4 charCodeAt(index)

```js
string.charCodeAt(index)
```

Behavior:
- If `index` is out of bounds → returns `NaN`
- Otherwise → returns the **Unicode code point** of the glyph at `index`

The code point is obtained by decoding the corresponding UTF-8 sequence.

---

## 5. Concatenation

The `+` operator concatenates strings as follows:
- UTF-8 buffers are concatenated
- The resulting buffer is rescanned
- A **new string** is created

Source strings remain unchanged.

---

## 6. Comparison

String comparisons are:
- Lexicographical
- Based on **Unicode code point order**
- Independent of UTF-8 byte length

---

## 7. Immutability

Strings are **strictly immutable**.

Any operation producing a string returns a **new instance**.

This property is mandatory for:
- memory safety
- object sharing
- garbage collection simplicity

---

## 8. Type Conversion

### ToString(value)

Conversion rules:
- `String` → unchanged
- `Number` → ASCII decimal representation
- `Boolean` → `"true"` / `"false"`
- `null` → `"null"`
- `undefined` → `"undefined"`

---

## 9. Conformance Notes

This specification intentionally diverges from ECMAScript 1 in the following areas:
- characters are **Unicode glyphs**, not UTF-16 code units
- `length` counts glyphs
- `charCodeAt` returns full Unicode code points

These deviations are **explicit, documented, and stable**.

---

## 10. Contract Summary

- UTF-8 internal storage
- glyph-based semantics
- O(1) glyph access
- immutable strings
- Unicode-correct behavior

This document is normative for the ProtoScript engine.