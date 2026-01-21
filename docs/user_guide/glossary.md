# ProtoScript Glossary

This glossary defines terms useful for understanding **ProtoScript** programs.

---

## ASCII
**American Standard Code for Information Interchange.** Defines the codes used to store characters in computers.

## BLOB
**Binary Large Object.** A format for binary data stored in a relational database.

## CGI
**Common Gateway Interface.** A specification for communication between an HTTP server and external programs running on that server.

## Core ProtoScript
The set of fundamental language elements of ProtoScript, derived from JavaScript 1.3. It includes a core set of objects (for example `Array`, `Date`, and `Math`) as well as core language constructs such as operators, control structures, and statements.

## Deprecate
To discourage use of a feature without removing it. A deprecated feature may be removed in a future release.

## ECMA
**European Computer Manufacturers Association.** An international standards organization for information and communication systems.

## ECMAScript
A standardized programming language based on core JavaScript. ProtoScript follows the behavior of JavaScript 1.3, prior to full ECMAScript standardization.

## External Function
A function defined in a native library that can be used from a ProtoScript program.

## HTTP
**Hypertext Transfer Protocol.** A communication protocol used to transfer information between networked systems.

## IP Address
A numerical address that identifies a host on a TCP/IP network.

## Primitive Value
Data that is represented directly at the lowest level of the language. In ProtoScript, a primitive value is a member of one of the following types: `undefined`, `null`, `Boolean`, `number`, or `string`.

Examples:
```
a = true        // Boolean
b = 42          // number
c = "Hello"    // string
x == undefined  // undefined
x == null       // null
```

## Static Method or Property
A method or property of a built-in object that is not accessible from instances of that object. For example, `Date.parse()` is a static method of `Date`.

