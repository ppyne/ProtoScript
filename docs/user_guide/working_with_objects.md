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
Io.writeLine(f, "hello");
Io.close(f);
```

Core operations:
- `Io.open(path, mode)` with `"r"`, `"w"`, `"a"`.
- `Io.read(file)` to read the rest of a file.
- `Io.readLines(file)` to read lines split on `Io.EOL` (`"\n"`).
- `Io.write(file, data)` and `Io.writeLine(file, data)`.
- `Io.close(file)` to release resources.
- `Io.print(string)` writes to `Io.stdout` without an implicit newline.

Standard streams:
- `Io.stdin`, `Io.stdout`, `Io.stderr` are always open and cannot be closed.

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

Buffers are also used for binary I/O and the Display framebuffer.

---

## Display module

ProtoScript exposes a host `Display` module for a single native window with a
software framebuffer. See the Display chapter for full behavior.

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
