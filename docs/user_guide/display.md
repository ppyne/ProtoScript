# ProtoScript — Display Module

This chapter documents the Display module and how to enable it at build time.

Display provides a single native window with a software framebuffer. It is
low-level, explicit, and does not include widgets, layouts, or event callbacks.

---

## Availability and Build Options

Display is optional and controlled by the `PS_ENABLE_MODULE_DISPLAY` build flag.

### Disable Display

If you do not want Display, build with:

```
make PS_ENABLE_MODULE_DISPLAY=0
```

In this mode the `Display` global is not defined (accessing it will raise a
`ReferenceError`), and `Event` will only return non-display events.

### Enable Display

To enable Display you need SDL2 (either system-wide or via `third_party/SDL`).

#### Option A: System SDL2

Install SDL2 so that `sdl2-config` is available in your `PATH`. On macOS,
this can be done via Homebrew or MacPorts.

Then build normally:

```
make
```

#### Option B: Vendored SDL2 (third_party)

The repo includes SDL2 as a git submodule in `third_party/SDL`.
Build it with CMake:

```
git submodule update --init --recursive third_party/SDL
mkdir -p third_party/SDL/build
cd third_party/SDL/build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

Then build ProtoScript:

```
make
```

Prerequisites for the vendored build:
- `cmake`
- a C compiler toolchain (Xcode Command Line Tools on macOS)

---

## Module Overview

Import:

```
import Display
```

Only one Display window can exist at a time (v1).

---

## API Reference

### Display.open(width, height, title, options)

Opens a window with a logical framebuffer.

Parameters:
- `width` (number): logical framebuffer width in pixels
- `height` (number): logical framebuffer height in pixels
- `title` (string): window title
- `options` (object, optional)

Options:
- `resizable` (boolean, default `false`)
- `scale` (string, default `"none"`)

Scale modes:
- `"none"`: no scaling, top-left aligned
- `"centered"`: no scaling, centered in the window
- `"fit"`: uniform scale to fit window, preserving aspect ratio
- `"stretch"`: scale to fill window (aspect not preserved)

Errors:
- throws if Display is already open
- throws if initialization fails

### Display.close()

Closes the window and releases native resources.

### Display.size()

Returns the logical framebuffer size:

```
{ width: number, height: number }
```

Returns:
- object with `width` and `height` (numbers)

### Display.clear(r, g, b)

Clears the logical framebuffer to a solid color.

Parameters:
- `r`, `g`, `b` (number): color components in range 0–255

### Display.pixel(x, y, r, g, b)

Sets one pixel in the logical framebuffer. Out-of-bounds is ignored.

Parameters:
- `x`, `y` (number): pixel coordinates
- `r`, `g`, `b` (number): color components in range 0–255

### Display.line(x1, y1, x2, y2, r, g, b)

Draws a line using integer rasterization.

Parameters:
- `x1`, `y1`, `x2`, `y2` (number): endpoints
- `r`, `g`, `b` (number): color components in range 0–255

### Display.rect(x, y, w, h, r, g, b)

Draws a rectangle outline.

Parameters:
- `x`, `y` (number): top-left corner
- `w`, `h` (number): width and height
- `r`, `g`, `b` (number): color components in range 0–255

### Display.fillRect(x, y, w, h, r, g, b)

Draws a filled rectangle.

Parameters:
- `x`, `y` (number): top-left corner
- `w`, `h` (number): width and height
- `r`, `g`, `b` (number): color components in range 0–255

### Display.framebuffer()

Returns the logical framebuffer as a `Buffer` (RGBA 8-bit).

Semantics:
- format: `R, G, B, A` bytes
- size: `width * height * 4`
- the buffer is live and stable across window resizes

Returns:
- `Buffer`

### Display.blitRGBA(buffer, srcW, srcH, dstX, dstY, destW?, destH?)

Copies an RGBA8 buffer into the logical framebuffer.

Parameters:
- `buffer` (`Buffer`): source RGBA data
- `srcW`, `srcH` (number): source dimensions in pixels
- `dstX`, `dstY` (number): destination top-left in the framebuffer
- `destW`, `destH` (number, optional): maximum size to write in the destination

Notes:
- The copy is clipped to the framebuffer bounds.
- Negative `dstX`/`dstY` values are allowed (source is clipped accordingly).
- If `destW`/`destH` are provided, the copy is limited to that size.

### Display.present()

Presents the logical framebuffer to the window using the selected scale mode.

Scaling is applied only at presentation time.

Returns:
- `undefined`

---

## Resize and Scaling Semantics

- Logical framebuffer size never changes automatically.
- Window resizing only affects presentation.
- No implicit framebuffer reallocation occurs on resize.

---

## Events

Display does not provide event callbacks. Events are pulled via `Event.next()`.

Event objects

`Event.next()` returns either `null` (no event) or an **event object**.
Every event object has at least:

```
{ type: string }
```

Some events include additional fields (see below). When an event has no extra
fields, it only contains `type`.

Display-related events:

### window_resized

Emitted when the window changes size (physical surface only).

Payload:

```
{ type: "window_resized", width: number, height: number }
```

### framebuffer_changed

Emitted when the logical framebuffer is replaced (e.g. reopening Display with
different logical dimensions).

Payload:

```
{ type: "framebuffer_changed", width: number, height: number }
```

### keydown

Emitted when a key is pressed.

```
{ type: "keydown", key: string, code: number, repeat: boolean }
```

### keyup

Emitted when a key is released.

```
{ type: "keyup", key: string, code: number }
```

### mousemotion

Emitted when the mouse moves.

```
{ type: "mousemotion", x: number, y: number, dx: number, dy: number }
```

### mousebuttondown

Emitted when a mouse button is pressed.

```
{ type: "mousebuttondown", button: number, x: number, y: number }
```

`button` is a number (platform-defined mapping).

### mousebuttonup

Emitted when a mouse button is released.

```
{ type: "mousebuttonup", button: number, x: number, y: number }
```

`button` is a number (platform-defined mapping).

### mousewheel

Emitted when the mouse wheel scrolls.

```
{ type: "mousewheel", dx: number, dy: number }
```

### quit

Emitted when the window close is requested.

```
{ type: "quit" }
```

---

## Example

```js
// Example with resizable window and scale-to-fit presentation.
Display.open(320, 240, "ProtoScript", { resizable: true, scale: "fit" });
Display.clear(234, 223, 201);
Display.fillRect(120, 70, 80, 60, 30, 90, 200);
Display.present();

while (true) {
    var ev = Event.next();
    if (ev && ev.type == "quit") {
        break;
    }
}

Display.close();
```
