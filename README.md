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

## Build (Simple, Complete)

### Prerequisites
- C compiler toolchain (gcc, clang, etc.)
- libjpeg (for Image module)
- libpng (for Image module)
- SDL2 (for Display module)
- git
- awk
- cmake
- make

### Install third_party submodules
```sh
git submodule update --init --recursive third_party/SDL third_party/libpng third_party/libjpeg
```

### Build third_party (SDL2, libpng, libjpeg)
```sh
mkdir -p third_party/SDL/build
cmake -S third_party/SDL -B third_party/SDL/build -DCMAKE_BUILD_TYPE=Release
cmake --build third_party/SDL/build

mkdir -p third_party/libpng/build
cmake -S third_party/libpng -B third_party/libpng/build -DPNG_SHARED=OFF -DPNG_TESTS=OFF
cmake --build third_party/libpng/build

mkdir -p third_party/libjpeg/build
cmake -S third_party/libjpeg -B third_party/libjpeg/build -DENABLE_SHARED=OFF
cmake --build third_party/libjpeg/build
```

### Build ProtoScript

```sh
make
```

### Test ProtoScript

```sh
make test
```

Note: test `155-display-blit-limit` opens an SDL window. In headless environments or CI,
`make test` will auto-skip it if a display cannot be created, even when
`PS_ENABLE_MODULE_DISPLAY=1`. To run this test, you need a working SDL2 setup with an
available display server (macOS with a logged-in GUI,
Linux X11/Wayland, or Windows desktop).

### Clean

Remove everything that was built.

```sh
make clean
```

### WebAssembly (WASM ou WebASM)

Note that ProtoScript is already be built in the `web` directory (you don't need to do the following).

If you really need to rebuild ProtoScript for WebAssembly, you must have Emscripten installed. Then you can do this:

```sh
make web-clean # clean the previous build
make web
```

### Configure features

You may want to personalize your build of ProtoScript with these options in the file `include/ps_config.h`:

- `PS_ENABLE_WITH` (0/1): enable `with`
- `PS_ENABLE_EVAL` (0/1): enable `eval`
- `PS_ENABLE_ARGUMENTS_ALIASING` (0/1): enable arguments aliasing
- `PS_ENABLE_MODULE_FS` (0/1): enable Fs module
- `PS_ENABLE_MODULE_IMG` (0/1): enable Image module (PNG/JPEG decode + resample)
- `PS_ENABLE_MODULE_DISPLAY` (0/1): enable Display module (SDL2)
- `PS_EVENT_QUEUE_CAPACITY` (int): Event queue size
- `PS_IMG_MAX_IMAGES` (int): max live images
- `PS_IMG_MAX_WIDTH` (int): max image width
- `PS_IMG_MAX_HEIGHT` (int): max image height

All feature switches are compile-time flags.

### Run ProtoScript

You can run ProtoScript in various ways:

```sh
./protoscript script.js
cat script.js | ./protoscript
echo 'Io.print("Hello world\n");'| ./protoscript
./protoscript < script.js
./protoscript - < script.js
```

### Examples

Examples show core language usage and host modules:

- `examples/misc/hello.js`: minimal run
- `examples/misc/basics.js`: variables, loops, functions
- `examples/misc/arrays.js`: arrays
- `examples/misc/strings.js`: strings
- `examples/misc/objects.js`: objects
- `examples/image/image_display.js`: PNG decode + Display blit

and more in the `examples/` directory.
