# ProtoScript

ProtoScript is a C reimplementation of an ECMAScript 1 core, designed as a minimal, embeddable scripting language.

The engine uses a glyph-based Unicode model (UTF-8), not UTF-16.
ProtoScript does not aim for modern JavaScript or browser compatibility.

## Examples

Run an example:

```sh
./protoscript examples/hello.js
```

Usage (file or stdin):

```sh
./protoscript script.js
cat script.js | ./protoscript
echo 'Io.print("Hello world");'| ./protoscript
./protoscript < script.js
./protoscript - < script.js
```

Other examples:

- `examples/basics.js`
- `examples/arrays.js`
- `examples/strings.js`
- `examples/objects.js`
