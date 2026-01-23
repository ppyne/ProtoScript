# ProtoScript — Display Module

**Specification v1.3**

This document defines the `Display` module for ProtoScript. The module provides a minimal, portable windowing and drawing surface backed by a native implementation (SDL2), without exposing SDL or any GUI framework concepts to user code.

The `Display` module is intentionally low-level and imperative. It does not provide widgets, layout, UI components, or event dispatch logic.

---

## 1. Scope and Goals

The Display module is responsible for:

- Creating and managing a native window
- Providing a pixel-addressable drawing surface (software rendering)
- Presenting rendered frames to the screen
- Exposing the framebuffer for explicit pixel manipulation via `Buffer`
- Defining explicit window resize and scaling semantics
- Acting as the source of low-level input events (forwarded to the core Event queue)

Out of scope:

- GUI widgets or controls
- Layout systems
- High-level drawing APIs (text, paths, fonts)
- Event dispatch, callbacks, or listeners
- Any browser-like behavior

---

## 2. Design Principles

- Explicit control flow
- No hidden state exposed to ProtoScript
- Deterministic behavior
- Cross-platform consistency (Windows, macOS, Linux)
- Minimal API surface
- Human-readable naming

The module represents *a display surface*, not a UI framework.

---

## 3. Module Overview

The module is imported as:

```
import Display
```

The module manages **at most one window** in version 1.

---

## 4. API Reference

### 4.1 `Display.open(width, height, title, options)`

Creates and opens a window with a software-rendered drawing surface.

**Parameters:**

- `width` (number): logical framebuffer width in pixels
- `height` (number): logical framebuffer height in pixels
- `title` (string): window title
- `options` (object, optional)

**Options:**

- `resizable` (boolean, default: `false`)
- `scale` (string, default: `"none"`)

**Scale modes:**

- `"none"`\
  → framebuffer is not scaled and is aligned to the **top-left** of the window

- `"centered"`\
  → framebuffer is not scaled and is **centered** in the window

- `"fit"`\
  → framebuffer is uniformly scaled to fit the window while preserving aspect ratio

- `"stretch"`\
  → framebuffer is scaled freely to fill the window (aspect ratio not preserved)

**Behavior:**

- Initializes the native backend
- Allocates a logical framebuffer (`Buffer`, RGBA 8-bit)
- Window resizing behavior is entirely defined by `options`

**Errors:**

- Throws if a display is already open
- Throws if initialization fails

---

### 4.2 `Display.close()`

Closes the window and releases all native resources.

---

### 4.3 `Display.size()`

Returns the logical framebuffer size.

**Returns:**

```
{ width: number, height: number }
```

---

### 4.4 `Display.clear(r, g, b)`

Clears the framebuffer with a solid color.

---

### 4.5 `Display.pixel(x, y, r, g, b)`

Sets a single pixel in the framebuffer.

Out-of-bounds coordinates are ignored.

---

### 4.6 `Display.line(x1, y1, x2, y2, r, g, b)`

Draws a straight line using an integer raster algorithm.

---

### 4.7 `Display.rect(x, y, w, h, r, g, b)`

Draws an axis-aligned rectangle outline.

---

### 4.8 `Display.fillRect(x, y, w, h, r, g, b)`

Draws a filled rectangle.

---

### 4.9 `Display.framebuffer()`

Returns the logical framebuffer as a `Buffer` for direct pixel manipulation.

**Semantics:**

- Format: RGBA 8-bit (4 bytes per pixel)
- Layout: `[R, G, B, A, ...]`
- Size: `width * height * 4`
- The buffer is **live** and stable across window resizes

---

### 4.10 `Display.present()`

Presents the framebuffer to the window according to the active scaling mode.

Scaling is applied **only at presentation time**.

---

## 5. Resize and Scaling Semantics

- The logical framebuffer size **never changes automatically**
- Window resizing affects only presentation
- No implicit framebuffer reallocation occurs

The framebuffer represents the **logical drawing surface**. The window represents the **physical presentation surface**.

---

## 6. Event Integration

The Display module does **not** expose event APIs.

All input and window events are forwarded to the core Event queue.

In addition, Display-related structural changes are reported via **explicit events**.

---

## 6.1 Display Events

### `framebuffer_changed`

Emitted when the **logical framebuffer is replaced**.

This event is generated only when:

- the framebuffer is reallocated explicitly by Display (future extensions), or
- a display is reopened with different logical dimensions.

This event is **not** emitted on window resize when the framebuffer remains unchanged.

**Event payload:**

```
{
  type: "framebuffer_changed",
  width: number,
  height: number
}
```

**Semantics:**

- Indicates that any previously obtained `Buffer` from `Display.framebuffer()` is invalid
- User code must reacquire the framebuffer via `Display.framebuffer()`

---

### `window_resized`

Emitted when the **window size changes** (physical surface only).

**Event payload:**

```
{
  type: "window_resized",
  width: number,
  height: number
}
```

**Semantics:**

- Does **not** imply any change to the logical framebuffer
- Useful for UI overlays, diagnostics, or presentation logic

---

### `keydown`

Emitted when a key is pressed.

**Event payload:**

```
{
  type: "keydown",
  key: string,
  code: number,
  repeat: boolean
}
```

**Semantics:**

- `key` is a human-readable key label (e.g. `"a"`, `"Escape"`)
- `code` is a stable numeric scancode (implementation-defined)
- `repeat` indicates auto-repeat

---

### `keyup`

Emitted when a key is released.

**Event payload:**

```
{
  type: "keyup",
  key: string,
  code: number
}
```

---

### `mousemotion`

Emitted when the mouse moves.

**Event payload:**

```
{
  type: "mousemotion",
  x: number,
  y: number,
  dx: number,
  dy: number
}
```

**Semantics:**

- `x`, `y` are coordinates in window space
- `dx`, `dy` are deltas since the last mouse motion event

---

### `mousebuttondown`

Emitted when a mouse button is pressed.

**Event payload:**

```
{
  type: "mousebuttondown",
  button: number,
  x: number,
  y: number
}
```

---

### `mousebuttonup`

Emitted when a mouse button is released.

**Event payload:**

```
{
  type: "mousebuttonup",
  button: number,
  x: number,
  y: number
}
```

---

### `mousewheel`

Emitted when the mouse wheel scrolls.

**Event payload:**

```
{
  type: "mousewheel",
  dx: number,
  dy: number
}
```

---

## 7. Rendering Model

- Pure software rendering
- Fixed RGBA 8-bit framebuffer
- No GPU API
- No retained drawing state

---

## 8. Explicit Non-Goals

- Automatic layout
- DPI scaling policies
- Text rendering
- Browser-like behavior

---

## 9. Roadmap Position

Display is the top layer in the graphics stack:

1. Buffer
2. Io (binary extension)
3. Event
4. Display

---

## 10. Philosophy

The Display module exists to answer one question:

> “Give me a window and let me draw pixels explicitly.”

Nothing more.
