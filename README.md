# ProtoScript

ProtoScript is a C reimplementation of an ECMAScript 1 core (almost JavaScript 1.3), designed as a minimal, embeddable scripting language.

It is a standalone command-line interpreter and does not target browsers, the DOM, HTML, or any client-side runtime.

The engine uses a glyph-based Unicode model (UTF-8), not UTF-16.

ProtoScript does not aim for modern JavaScript or browser compatibility.

## Documentation

- User guide: [`docs/user_guide/toc.md`](docs/user_guide/toc.md)
- ES1 deviations: [`docs/es1-notes.md`](docs/es1-notes.md)
- String model spec: [`docs/string-spec.md`](docs/string-spec.md)
- Io module spec: [`docs/io_spec.md`](docs/io_spec.md)
- GC spec: [`docs/gc_spec.md`](docs/gc_spec.md)
- ECMA-262 Edition 1 (June 1997): [`docs/ECMA-262_1st_edition_june_1997.pdf`](docs/ECMA-262_1st_edition_june_1997.pdf)
- License (BSD-3-Clause): [`LICENSE`](LICENSE)

## Build

No external dependencies are required.

```sh
make
```

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
