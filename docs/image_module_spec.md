# ProtoScript Image Module — Specification v1.0

## Overview

The `Image` module provides **image decoding and resampling facilities** for ProtoScript.

Its responsibility is strictly limited to:
- decoding image formats into an **in-memory RGBA8 representation**
- resampling RGBA images using well-defined interpolation modes

The module operates exclusively on `Buffer` objects and plain ProtoScript objects.

---

## Availability & Compilation

The Image module is **optional** and **disabled at compile time** via `ps_config.h`.

```c
#define PS_ENABLE_MODULE_IMG 1
```

- `1` : Image module enabled
- `0` : Image module disabled (module not registered, no symbols exposed)

When `PS_ENABLE_MODULE_IMG == 0`:
- the Image module MUST NOT be registered
- PNG/JPEG third-party libraries MUST NOT be linked
- no object code related to image decoding or resampling must be included

The module depends on:
- `src/img_resample.c`
- `include/ps_img_resample.h`

Third-party image decoders are integrated as **git submodules** under `third_party/` (see below).

---

## Image Object Definition

The Image module uses a **plain object representation** for images.

```text
Image {
  width:  number,
  height: number,
  data:   Buffer
}
```

### Semantics

- `width`  : image width in pixels
- `height` : image height in pixels
- `data`   : raw RGBA8 buffer

### Data Layout

- destination pixel format: **RGB8 or RGBA8 internally normalized**
- byte order per pixel: `R, G, B, A`
- alpha: **always present**, non-premultiplied
- origin: **top-left corner (0,0)**
- storage order: **row-major**, no padding

### Color Conversion Rules

All decoded images are converted to **RGBA8**:

- RGB images → R,G,B copied, A set to 255
- Grayscale images → gray value replicated to R,G,B, A set to 255
- Grayscale + alpha → gray replicated to R,G,B, alpha preserved
- Paletted images → expanded to RGBA

No color space management is performed (no ICC, no gamma correction).

### Invariant

```text
data.length == width × height × 4
```

Functions in this module MUST validate this invariant on input.

---

## Third-Party Libraries

Image decoding relies on well-established, permissively licensed libraries, embedded as **git submodules**.

### PNG decoding

- Library: **libpng**
- Location: `third_party/libpng/`
- Scope: all PNG variants supported by libpng

### JPEG decoding

- Library: **libjpeg-turbo** (or libjpeg compatible)
- Location: `third_party/libjpeg/`
- Scope: all JPEG variants supported by the library

### Build Rules

- The build system (Makefile) MUST link these libraries **only if** `PS_ENABLE_MODULE_IMG == 1`
- When disabled, no include paths, objects, or linker flags related to these libraries may be present

---

## API Reference

### Image.decodePNG

```text
Image.decodePNG(buffer: Buffer) -> Image
```

Decodes a PNG image from a binary buffer using **libpng**.

All PNG formats supported by libpng are accepted and converted to RGBA8.

#### Errors
- invalid PNG data → `DecodeError`
- unsupported PNG features → `DecodeError`
- Image module disabled → `ModuleDisabledError`

---(buffer)

```text
Image.decodePNG(buffer: Buffer) -> Image
```

Decodes a PNG image from a binary buffer.

#### Errors
- invalid PNG data → `DecodeError`
- unsupported PNG features → `DecodeError`
- Image module disabled → `ModuleDisabledError`

---

### Image.decodeJPEG

```text
Image.decodeJPEG(buffer: Buffer) -> Image
```

Decodes a JPEG image from a binary buffer using **libjpeg(-turbo)**.

All JPEG formats supported by the library are accepted and converted to RGBA8.

Grayscale JPEG images are expanded by replicating the gray channel to R, G and B.

#### Errors
- invalid JPEG data → `DecodeError`
- unsupported JPEG features → `DecodeError`
- Image module disabled → `ModuleDisabledError`

---(buffer)

```text
Image.decodeJPEG(buffer: Buffer) -> Image
```

Decodes a JPEG image from a binary buffer.

#### Errors
- invalid JPEG data → `DecodeError`
- unsupported JPEG features → `DecodeError`
- Image module disabled → `ModuleDisabledError`

---

### Image.resample(image, newWidth, newHeight, mode)

```text
Image.resample(
  image: Image,
  newWidth: number,
  newHeight: number,
  mode?: string
) -> Image
```

Resamples an RGBA image to a new resolution.

#### Supported Modes

| Mode   | Description                         |
|--------|-------------------------------------|
| none   | Nearest‑neighbor (no interpolation) |
| linear | Bilinear interpolation              |
| cubic  | Bicubic interpolation (default)     |
| nohalo | NoHalo interpolation                |
| lohalo | LoHalo interpolation                |

If `mode` is omitted or `null`, **`cubic` is used by default**.

#### Validation Rules

- `image` MUST satisfy the Image invariant
- `newWidth`  > 0
- `newHeight` > 0
- `mode` MUST be one of: `none`, `linear`, `cubic`

Invalid inputs MUST raise `ArgumentError`.

---

### Image.detectFormat

```text
Image.detectFormat(buffer: Buffer) -> string | null
```

Detects the format of an image buffer by signature.

#### Return values
- `"png"` for PNG signature
- `"jpeg"` for JPEG signature
- `null` if unknown or too short

#### Errors
- invalid buffer → `ArgumentError`

---

## Error Semantics

| Condition                                   | Error                 |
|--------------------------------------------|-----------------------|
| Module disabled                              | ModuleDisabledError   |
| Invalid image object                        | ArgumentError         |
| Invalid buffer size                         | ArgumentError         |
| Unsupported decode format                  | DecodeError           |
| Invalid resampling mode                    | ArgumentError         |
| Unknown format (detectFormat)              | None (returns null)   |

No function may fail silently.

---

## Test Matrix

### Decode Tests

- valid PNG → correct width/height/data
- valid JPEG → correct width/height/data
- corrupted file → DecodeError
- empty buffer → DecodeError

### Resample Tests

- upscale (nearest, linear, cubic)
- downscale (nearest, linear, cubic)
- identity resample (same size)
- invalid mode
- zero or negative dimensions

### Invariant Tests

- incorrect buffer length
- missing properties
- non‑numeric width/height

---

## Documentation Requirements

All documentation MUST be updated accordingly:

- API reference
- User Guide
- Module list
n### User Guide MUST include

- loading an image via `Io.open`
- decoding PNG and JPEG
- resampling with each mode
- explanation of RGBA memory layout

Examples MUST:
- be placed in the `examples/` directory (or subdirectories)
- be tested at least once
- compile and run without errors

---

## Limits & Resource Considerations

The Image module does not impose hard-coded format limits, but implementations MAY enforce **configurable resource limits** at compile time. These limits are designed to align with ProtoScript GC behavior and to prevent pathological memory pressure.

### Default Reference Capability

A reference implementation MUST be able to handle concurrently:

- **8 images**
- resolution up to **7680 × 4320 pixels (8K UHD)**
- format **RGBA8**

Memory footprint:

```text
7680 × 4320 × 4 ≈ 132 MiB per image
8 images ≈ 1.05 GiB
```

This budget is considered acceptable on modern desktop systems and intentionally represents a **high but realistic upper bound**.

### Compile-Time Configuration (`ps_config.h`)

```c
#define PS_IMG_MAX_IMAGES   8
#define PS_IMG_MAX_WIDTH    7680
#define PS_IMG_MAX_HEIGHT   4320
```

Implementations MAY:
- lower these values for constrained systems
- raise them on systems with sufficient memory

If a limit is exceeded:
- decoding or resampling MUST fail with `ResourceLimitError`
- no partial image object may be returned

### Interaction with the Garbage Collector

Image objects are regular ProtoScript objects and therefore fully subject to GC tracking.

However, due to their **large contiguous memory footprint**, implementations MUST treat Image allocations as **high-pressure allocations**:

- allocation of an Image buffer SHOULD trigger a GC cycle if memory pressure thresholds are reached
- implementations MAY apply lower GC thresholds when one or more Image objects are live
- GC heuristics SHOULD assume that Image buffers dominate retained memory compared to typical objects

The Image module MUST NOT attempt to bypass or manage GC memory independently.

---

## Rationale: Why 8 × 8K Images

The chosen reference limit (8 images at 8K UHD) is intentional and conservative.

Rationale:

- **8K (7680×4320)** represents a modern upper bound for consumer and professional imagery (photography, video frames, textures)
- **RGBA8** is the most common uncompressed working format for image pipelines
- **8 concurrent images** covers realistic workflows such as:
  - double-buffering
  - multi-pass processing
  - source + intermediate + destination images

From a memory perspective:

- ~1 GiB of image data fits comfortably within typical 16–32 GiB systems
- it avoids accidental denial-of-service scenarios caused by unbounded image decoding

This limit is **not a functional restriction**, but a **safety and predictability contract** between the Image module, the runtime, and the garbage collector.

Implementations remain free to raise or lower these limits at compile time based on deployment constraints.

---

## Non‑Goals (v1)

- no color space conversion
- no premultiplied alpha
- no image encoding
- no metadata exposure

---

## Versioning

- Initial version: **Image module v1.0**
- Any new pixel format or encoding support MUST bump the minor version
