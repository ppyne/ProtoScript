# ProtoScript — ES1 Deviations Only

This document lists only the remaining deviations from ECMA-262 Edition 1.

## 1. String model (UTF-8 / glyphs)

ProtoScript uses UTF-8 with glyph-based length and indexing:
- `length` counts glyphs (not UTF-16 code units).
- `charCodeAt` returns full Unicode code points.
- Glyph length is validated by `tests/cases/117-glyph-length.ps`.

## 2. RegExp ignoreCase

`ignoreCase` uses a static one-to-one case-folding table (ASCII + Latin-1 +
Latin Extended-A + basic Greek + basic Cyrillic) plus special-case folds
(final sigma, long s, Kelvin sign, Angstrom sign, sharp s). It does not
support full Unicode case-folding or multi-character folds.

## 3. Arguments aliasing default

Full `arguments` <-> parameter aliasing is enabled only when
`PS_ENABLE_ARGUMENTS_ALIASING=1`. Default behavior is configurable.

## 4. Host extension

`Io.print` is a non-standard host extension.
