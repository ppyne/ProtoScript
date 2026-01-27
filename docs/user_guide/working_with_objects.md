![ProtoScript](../../header.png)

# Chapter 7 — Working with Objects

ProtoScript is designed around a simple **object-based paradigm**. An object is a collection of properties, where each property is either a value or another object. Objects can also have associated functions, called **methods**.

This chapter explains how to work with objects, properties, and methods, and how to create your own object types.

---

## Objects and Properties

You access object properties using either **dot notation** or **bracket notation**:

```js
objectName.propertyName;
objectName["propertyName"];
```

Property names are case-sensitive. You create or modify a property by assigning a value to it:

```js
myCar.make = "Ford";
myCar.model = "Mustang";
myCar.year = 1969;
```

Bracket notation allows dynamic property access:

```js
myCar["make"] = "Ford";
myCar["model"] = "Mustang";
myCar["year"] = 1967;
```

Objects and arrays share the same underlying structure. When string keys are used, this pattern is often referred to as an **associative array**.

### Enumerating Properties

```js
function showProps(obj, name) {
    var result = "";
    for (var i in obj)
        result += name + "." + i + " = " + obj[i] + "\n";
    return result;
}
```

---

## Creating New Objects

ProtoScript provides several ways to create objects.

### Using Object Initializers

Object initializers (also called *object literals*) allow objects to be created concisely:

```js
myHonda = {
    color: "red",
    wheels: 4,
    engine: { cylinders: 4, size: 2.2 }
};
```

Object literals are evaluated each time they appear in the code.

---

### Using Constructor Functions

You can define a new object type by writing a constructor function and instantiating it with `new`.

```js
function Car(make, model, year) {
    this.make = make;
    this.model = model;
    this.year = year;
}
```

```js
myCar = new Car("Eagle", "Talon TSi", 1993);
```

Objects may reference other objects:

```js
function Person(name, age) {
    this.name = name;
    this.age = age;
}

owner = new Person("Rand", 33);
car1 = new Car("Eagle", "Talon", 1993);
car1.owner = owner;
```

---

## Indexing Object Properties

Properties created with string names must always be accessed using string keys. Properties created using numeric indices must always be accessed using numeric indices.

---

## Defining Properties on an Object Type

Shared properties can be added using the `prototype` property:

```js
Car.prototype.color = null;
car1.color = "black";
```

All instances of `Car` share properties defined on `Car.prototype`.

Prototype utilities:
- `Object.getPrototypeOf(obj)` to inspect the prototype.
- `Object.create(proto)` to create an object with a specific prototype.
- `Object.setPrototypeOf(obj, proto)` to update the prototype (use with care).

---

## Defining Methods

A method is a function associated with an object.

```js
function displayCar() {
    return this.year + " " + this.make + " " + this.model;
}
```

```js
Car.prototype.display = displayCar;
```

```js
car1.display();
```

---

## Using `this`

Inside a method, the keyword `this` refers to the object on which the method was called.

```js
function validate(obj, low, high) {
    if ((obj.value < low) || (obj.value > high))
        return false;
    return true;
}
```

All browser-specific event examples have been removed.

---

## Deleting Properties and Objects

The `delete` operator removes a property or object reference:

```js
obj = new Number(10);
delete obj;
```

Removing the last reference to an object allows it to be garbage-collected.

---

## Predefined Core Objects

ProtoScript provides the following predefined core objects:

- `Object`
- `Array`
- `Function`
- `String`
- `Number`
- `Boolean`
- `Date`
- `Math`
- `RegExp`
- `JSON`
- `Io`
- `Gc`

Browser-only objects have been intentionally excluded.

---

## Notes on Core Objects

### Array

Arrays are ordered collections indexed from zero.

```js
items = ["Wind", "Rain", "Fire"];
```

Standard methods include `push`, `pop`, `slice`, `splice`, `sort`, and others.

---

### Date

The `Date` object represents time as milliseconds since January 1, 1970 (UTC).

```js
today = new Date();
```

---

### Math

`Math` provides constants and mathematical functions:

```js
Math.PI;
Math.sin(1.56);
```

The `Math` object is static and cannot be instantiated.

---

### String

String literals are preferred over `String` objects:

```js
s1 = "2 + 2";
s2 = new String("2 + 2");
```

String methods such as `substring`, `split`, `match`, and `replace` operate on both literals and objects.

Note: ProtoScript uses a glyph-based UTF-8 string model (not ES1/JS1.x UTF-16).
`length` counts glyphs and `charCodeAt` returns full Unicode code points.

---

## JSON

ProtoScript provides a global `JSON` object with `parse` and `stringify`
(an ES5 feature, not part of ES1).

```js
var data = JSON.parse("{\"a\":1,\"b\":[true,\"x\"]}");
Io.print((JSON.stringify(data)) + "\n");
```

Limitations:
- No `reviver` for `JSON.parse`.
- No `replacer` or `space` for `JSON.stringify`.
- No `toJSON` hook.
- Circular structures throw a `TypeError`.
- Non-finite numbers stringify as `null`.

---

## Io module

ProtoScript exposes a host `Io` module for synchronous, explicit I/O.

```js
var path = Io.tempPath();
var f = Io.open(path, "w");
f.write("hello\n");
f.close();
```

Core operations:
- `Io.open(path, mode)` returns a file object.
- `file.read()` reads to EOF and returns a string (text) or `Buffer` (binary).
- `file.read(size)` reads up to `size` bytes and returns `Io.EOF` at end-of-file.
- `file.write(data)` writes a string (text mode) or `Buffer` (binary mode).
- `file.close()` closes the file (explicit, no GC auto-close).
- `Io.EOF` is a unique constant used for end-of-file detection.
- `Io.print(string)` writes to `Io.stdout` without an implicit newline.
- `Io.sprintf(format, ...args)` formats values into a string (no I/O).

Mode flags:
- `"r"` read text, `"w"` write text, `"a"` append text.
- Add `"b"` for binary: `"rb"`, `"wb"`, `"ab"`.

Writing to stderr:

```js
Io.stderr.write("error\n");
```

You can also use:

```js
console.error("error", "detail");
```

Standard streams:
- `Io.stdin`, `Io.stdout`, `Io.stderr` are always open and cannot be closed.

Formatting:
`Io.sprintf(format, ...args)` builds and returns a formatted string.

### Format specification

Each format specifier follows:

```
%[flags][width][.precision]specifier
```

Only the specifiers and modifiers below are supported.

#### Flags

- `0` — zero padding (ignored when left-aligned).
- `-` — left alignment.

#### Width

Minimum field width. If the formatted value is shorter, it is padded with
spaces (or zeros if `0` is used).

#### Precision

Only applies to `%f` and specifies the number of digits after the decimal
point.

#### Supported specifiers

| Specifier | Description |
| -------- | -------- |
| `%s` | string (uses `String(value)`) |
| `%d` | signed decimal integer |
| `%i` | signed integer |
| `%x` | hexadecimal (lowercase) |
| `%X` | hexadecimal (uppercase) |
| `%o` | octal |
| `%f` | floating-point |
| `%%` | literal percent sign |

#### Notes and limitations

- Width and precision must be numeric literals.
- Precision is only supported for `%f`.
- Unsupported format sequences are left unchanged.
- Missing arguments format as `undefined`.

```js
var line = Io.sprintf("%-10s %8.2f", "total", 3.14159);
Io.print(line + "\n");
```

Sequential read example:

```js
var f = Io.open("data.bin", "rb");
while (true) {
    var chunk = f.read(1024);
    if (chunk === Io.EOF) break;
    // process chunk
}
f.close();
```

File objects:
- `file.path`: original path string.
- `file.mode`: mode string.
- `file.closed`: boolean.

---

## Fs module

ProtoScript exposes a synchronous filesystem module `Fs`. It is **POSIX-only**
(Linux, BSD, macOS). The module is controlled by the compile-time flag
`PS_ENABLE_MODULE_FS` (1 = enabled, 0 = disabled). When disabled, `Fs` is not
defined.

All `Fs` functions are explicit and return values instead of throwing:
- boolean operations return `true`/`false`
- `Fs.size` returns `undefined` on failure
- `Fs.ls` returns an empty array on failure

Operations:
- `Fs.chmod(path, mode)`
- `Fs.cp(source, destination)`
- `Fs.exists(path)`
- `Fs.size(path)`
- `Fs.isDir(path)`
- `Fs.isFile(path)`
- `Fs.isSymlink(path)`
- `Fs.isExecutable(path)`
- `Fs.isReadable(path)`
- `Fs.isWritable(path)`
- `Fs.ls(path, all = false, limit = 0)`
- `Fs.mkdir(path)`
- `Fs.mv(source, destination)`
- `Fs.pathInfo(path)`
- `Fs.pwd()`
- `Fs.cd(path)`
- `Fs.rmdir(path)`
- `Fs.rm(path)`

Examples (from the Fs specification):

```js
Fs.chmod("my_file.txt", 0644);
```

```js
Fs.cp("a.txt", "b.txt");
```

```js
Fs.exists("existing.txt");
```

```js
Fs.size("somefile.txt");
```

```js
Fs.isDir("..");
```

```js
Fs.isFile("README.md");
```

```js
Fs.isSymlink("link.txt");
```

```js
var path = "./";
var files = Fs.ls(path);
for (file of files) {
    if (Fs.isDir(path + file)) Io.print("directory: " + file + Io.EOL);
    else Io.print("file: " + file + Io.EOL);
}
```

```js
function walkTree(path, prefix = "") {
    if (path.lastIndexOf('/') !== path.length - 1) path += "/";
    var files = Fs.ls(path, true, 200);
    for (file of files) {
        if (file == "." || file == "..") continue;
        if (Fs.isSymlink(path + file)) {
            Io.print(prefix + "+- " + file + "@" + Io.EOL);
            continue;
        }
        var full = path + file;
        if (Fs.isDir(full)) {
            Io.print(prefix + "+- " + file + "/" + Io.EOL);
            walkTree(full, prefix + "|  ");
        } else {
            Io.print(prefix + "+- " + file + Io.EOL);
        }
    }
}
walkTree("./");
```

```js
Fs.mkdir("tmp");
```

```js
Fs.mv("a.txt", "b.txt");
```

```js
var infos = Fs.pathInfo("/a/b.txt");
for (info in infos) Io.print(info + ': ' + infos[info] + Io.EOL);
```

```js
Fs.pwd();

Fs.cd("tmp");
```

```js
var path = "example";
if (Fs.isDir(path) && Fs.ls(path).length == 0) {
    Fs.rmdir(path);
}
```

```js
if (Fs.exists(filename)) Fs.rm(filename);
```

Additional practical examples:

Directory traversal:

```js
function listDirs(path) {
    var out = [];
    var files = Fs.ls(path, true, 200);
    for (file of files) {
        var full = path + "/" + file;
        if (Fs.isDir(full)) out[out.length] = full;
    }
    return out;
}
```

Permission handling:

```js
var target = "script.sh";
if (Fs.exists(target)) {
    Fs.chmod(target, 0755);
    if (Fs.isExecutable(target)) Io.print("ready" + Io.EOL);
}
```

Error-safe filesystem manipulation:

```js
var src = "input.txt";
var dst = "output.txt";
if (Fs.isFile(src)) {
    if (!Fs.cp(src, dst)) Io.print("copy failed" + Io.EOL);
}
```

Typical scripting use case:

```js
var tmp = "tmp";
if (!Fs.exists(tmp)) Fs.mkdir(tmp);
var files = Fs.ls(".");
for (file of files) {
    if (Fs.isFile(file) && file.lastIndexOf(".log") > 0) {
        Fs.mv(file, tmp + "/" + file);
    }
}
```

---

## Event module

ProtoScript exposes a host `Event` module for pull-based event access.

```js
var ev = Event.next();
if (ev && ev.type == "quit") {
    Io.print("bye\n");
}
```

See the Display chapter for event types and payloads.

---

## Buffer module

ProtoScript exposes a low-level `Buffer` module for byte-addressable data.

```js
var b = Buffer.alloc(4);
b[0] = 255;
Io.print(Buffer.size(b) + "\n");
```

Buffer operations:
- `Buffer.alloc(size)` allocates a zeroed buffer.
- `Buffer.size(buf)` returns the size in bytes.
- `Buffer.slice(buf, offset, length)` copies a sub-range.
- `buf[i]` reads a byte; `buf[i] = value` writes a byte after `ToNumber` conversion, rounding, and clamping.
- `NaN`/`Infinity`/`-Infinity` become `0`; values in `(0, 1)` become `0`; other values round via `floor(x + 0.5)` and clamp to `0–255`.
- Out-of-range access throws an error.

Buffers are also used for binary I/O and the Display framebuffer.

---

## Buffer32 module

Buffer32 provides a native 32-bit view over byte buffers `Buffer`.

```js
var buf = Buffer.alloc(8);
var view = Buffer32.view(buf);
view[0] = 0x04030201;
Io.print(view[0] + "\n");
```

Buffer32 operations:
- `Buffer32.alloc(length)` allocates a zeroed 32-bit buffer view.
- `Buffer32.size(buf32)` returns element count (uint32).
- `Buffer32.byteLength(buf32)` returns byte size.
- `Buffer32.view(buffer, offset?, length?)` creates a view (no copy).
- `buf32[i]` reads a uint32; `buf32[i] = value` writes after `ToNumber` conversion, rounding, and clamping to `0–4294967295`.
- Out-of-range access throws an error.

The view is live; writes update the underlying `Buffer`.

---

## Image module

ProtoScript exposes a host `Image` module for decoding PNG/JPEG buffers,
encoding RGBA images to PNG/JPEG, and resampling RGBA images. It is available
when `PS_ENABLE_MODULE_IMG` is set to `1` at build time.

Image objects are plain objects:

```js
{
    width:  320,
    height: 200,
    data:   Buffer.alloc(320 * 200 * 4)
}
```

The `data` buffer is RGBA8 (R, G, B, A), non-premultiplied, row-major, and
originates at the top-left pixel (0,0).

```js
var f = Io.open("assets/photo.png", "rb");
var buf = f.read();
f.close();

var format = Image.detectFormat(buf);
var img;
if (format == 'png') img = Image.decodePNG(buf);
else if (format == 'jpeg') img = Image.decodeJPEG(buf);
else {
    Io.stderr.write("Unsupported image format\n");
    ProtoScript.exit(0);
}

var half = Image.resample(img, img.width / 2, img.height / 2, "linear");
Io.print(half.width + "x" + half.height + "\n");
```

Resample modes: `none`, `linear`, `cubic`, `nohalo`, `lohalo`.

To encode images:

```js
var png = Image.encodePNG(img);
var out = Io.open("out.png", "wb");
out.write(png);
out.close();

var jpeg = Image.encodeJPEG(img, 85);
var outJ = Io.open("out.jpg", "wb");
outJ.write(jpeg);
outJ.close();
```

---

## Display module

ProtoScript exposes a host `Display` module for a single native window with a
software framebuffer. See the [Display chapter](display.md) for full behavior.

---

## Gc module

ProtoScript exposes a host `Gc` module for explicit garbage-collection control
and introspection.

```js
Gc.collect();
var stats = Gc.stats();
Io.print(stats.collections + "\n");
```

`Gc.stats()` returns an object with:
- `totalBytes`: current heap bytes tracked by the GC.
- `liveBytes`: bytes live after the last collection.
- `collections`: number of collections so far.
- `freedLast`: objects freed in the last collection.
- `threshold`: next automatic collection threshold.

GC never closes external resources; `Io.close(...)` is always required.
