![ProtoScript](../header.png)

# ProtoScript — ES1 Deviations Only

This document lists only the remaining deviations from [ECMA-262 Edition 1](ECMA-262_1st_edition_june_1997.pdf).

## 1. String model (UTF-8 / glyphs)

ProtoScript uses UTF-8 with glyph-based length and indexing:
- `length` counts glyphs (not UTF-16 code units).
- `charCodeAt` returns full Unicode code points.
- Glyph length is validated by `tests/cases/117-glyph-length.js`.

## 2. RegExp ignoreCase

`ignoreCase` uses a static one-to-one case-folding table (ASCII + Latin-1 +
Latin Extended-A + basic Greek + basic Cyrillic) plus special-case folds
(final sigma, long s, Kelvin sign, Angstrom sign, sharp s). It does not
support full Unicode case-folding or multi-character folds.

## 3. Arguments aliasing default

Full `arguments` <-> parameter aliasing is enabled only when
`PS_ENABLE_ARGUMENTS_ALIASING=1`. The default is `PS_ENABLE_ARGUMENTS_ALIASING=0`,
set in `include/ps_config.h`.

## 4. `eval` / `with` flags

`eval` and `with` exist but are compile-time configurable. The defaults are
`PS_ENABLE_EVAL=0` and `PS_ENABLE_WITH=0`, set in `include/ps_config.h`. When
disabled, attempting to use them is rejected.

## 5. Host extension

The `Io` module (print + file/stream I/O) is a non-standard host extension.

## 6. GC module (host extension)

ProtoScript exposes a non-standard `Gc` object with `Gc.collect()` and
`Gc.stats()` to force and inspect garbage collection. This is outside ES1.

## 7. ES2015 default parameters

ProtoScript supports ES2015-style default parameter values in function
declarations (for example `function f(x, y = 10) { ... }`). This is an ES6
extension beyond ES1.

## 8. JSON (ES5)

ProtoScript exposes a global `JSON` object with `JSON.parse` and
`JSON.stringify`. This is an ES5 feature (not part of ES1). The implementation
does not support revivers, replacers, spacing, or `toJSON`.

## 9. `for...of` (ES2015)

ProtoScript supports a simplified `for...of` loop (ES2015/ES6). It iterates:
- array values in index order,
- own enumerable property values of plain objects,
- glyphs of UTF-8 strings.

This is an ES6 extension beyond ES1 and does not use the ES6 iterator protocol.

## 10. `instanceof` (ES3)

ProtoScript supports the `instanceof` operator (added in ES3). The right-hand
side must be a function with a valid `prototype`, otherwise a `TypeError` is
raised.
