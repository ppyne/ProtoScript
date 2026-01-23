![ProtoScript](header.png)

# ProtoScript

ProtoScript is a C reimplementation of an ECMAScript 1 core (almost JavaScript 1.3), designed as a minimal, embeddable scripting language.

It is a standalone command-line interpreter and does not target browsers, the DOM, HTML, or any client-side runtime.

The engine uses a glyph-based Unicode model (UTF-8), not UTF-16.

ProtoScript does not aim for modern JavaScript or browser compatibility.

## Manifest

[Prototype-Based Languages: **A Different Way to Think**](docs/prototype_based_thinking_with_protoscript.md)

## Demo

You can test ProtoScript immediately thanks to a demo running on the web via WebAssembly (compiled with emscripten):

**Try it now:** [`ppyne.github.io/ProtoScript/`](https://ppyne.github.io/ProtoScript/)

## Documentation

- The technical foundation: [Object Creation with ES1](docs/object_creation.md)
- User guide: [`docs/user_guide/toc.md`](docs/user_guide/toc.md)
- Language reference: [`docs/language_reference.md`](docs/language_reference.md)
- ES1 deviations: [`docs/es1-notes.md`](docs/es1-notes.md)
- String model spec: [`docs/string-spec.md`](docs/string-spec.md)
- Io module spec: [`docs/io_unified_spec.md`](docs/io_unified_spec.md)
- GC spec: [`docs/gc_spec.md`](docs/gc_spec.md)
- ECMA-262 Edition 1 (June 1997): [`docs/ECMA-262_1st_edition_june_1997.pdf`](docs/ECMA-262_1st_edition_june_1997.pdf)
- License (BSD-3-Clause): [`LICENSE`](LICENSE)

## Build

Display module is optional (native window + software framebuffer for drawing pixels) and controlled by the `PS_ENABLE_MODULE_DISPLAY` build flag.

Image module is optional (PNG/JPEG decode + resampling) and controlled by `PS_ENABLE_MODULE_IMG` in `include/ps_config.h`.

### Disable Display

If you do not want Display, build with:

```sh
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

```sh
make
```

#### Option B: Vendored SDL2 (third_party)

The repo includes SDL2 as a git submodule in `third_party/SDL`.
Build it with CMake:

```sh
git submodule update --init --recursive third_party/SDL
mkdir -p third_party/SDL/build
cd third_party/SDL/build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

Prerequisites for the vendored build:
- SDL2
- `cmake`
- a C compiler toolchain (Xcode Command Line Tools on macOS)

---

If you do not use Display, no external dependencies are required.

```sh
make
```

If you enable Image, make sure libpng and libjpeg are available (vendored as
submodules under `third_party/` or provided system-wide).

### Enable Image

To use vendored libraries:

```sh
git submodule update --init --recursive third_party/libpng third_party/libjpeg
mkdir -p third_party/libpng/build
cmake -S third_party/libpng -B third_party/libpng/build -DPNG_SHARED=OFF -DPNG_TESTS=OFF
cmake --build third_party/libpng/build
mkdir -p third_party/libjpeg/build
cmake -S third_party/libjpeg -B third_party/libjpeg/build -DENABLE_SHARED=OFF
cmake --build third_party/libjpeg/build
```

Then set `PS_ENABLE_MODULE_IMG` to `1` and run `make`.

## Tests

```sh
make test
```

## Examples

Run an example:

```sh
./protoscript examples/hello.js
```

Usage (file or stdin):

```sh
./protoscript script.js
cat script.js | ./protoscript
echo 'Io.print("Hello world\n");'| ./protoscript
./protoscript < script.js
./protoscript - < script.js
```

Other examples:

- `examples/basics.js`
- `examples/arrays.js`
- `examples/strings.js`
- `examples/objects.js`
