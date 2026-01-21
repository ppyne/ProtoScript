![ProtoScript](../../header.png)

# Chapter 8 — Object Model and Prototypes

ProtoScript is a **prototype-based language**, not a class-based one. Objects inherit properties directly from other objects through the prototype chain. This chapter explains in detail how constructor functions, prototypes, and inheritance work in ProtoScript.

---

## Class-Based vs. Prototype-Based Languages

Class-based languages (such as Java or C++) distinguish between **classes** and **instances**. A class defines a fixed structure, and instances are concrete realizations of that class.

ProtoScript does not make this distinction. It uses **prototypical objects** instead:
- Any object can serve as a prototype for another object.
- Properties can be added or removed at run time.
- Inheritance is resolved dynamically by traversing the prototype chain.

Constructor functions replace class definitions. Any function can be used as a constructor when called with the `new` operator.

---

## Constructor Functions and Inheritance

A constructor function initializes new objects:

```js
function Employee() {
    this.name = "";
    this.dept = "general";
}
```

Objects are created using the `new` operator:

```js
mark = new Employee();
```

To establish inheritance, assign an object to the constructor’s `prototype` property:

```js
function Manager() {
    this.reports = [];
}
Manager.prototype = new Employee();
```

An object created with `new Manager()` inherits properties from `Employee` through the prototype chain.

---

## Creating an Object Hierarchy

The following example illustrates a simple inheritance hierarchy:

```js
function WorkerBee() {
    this.projects = [];
}
WorkerBee.prototype = new Employee();

function Engineer() {
    this.dept = "engineering";
    this.machine = "";
}
Engineer.prototype = new WorkerBee();
```

An `Engineer` object inherits properties from both `WorkerBee` and `Employee`.

---

## Object Properties and the Prototype Chain

When a property is accessed, ProtoScript follows these steps:

1. Check whether the property exists locally on the object.
2. If not, check the object’s prototype.
3. Continue traversing the prototype chain until the property is found or the chain ends.

```js
mark = new WorkerBee();
mark.name;      // inherited from Employee
mark.projects;  // local to WorkerBee
```

---

## Adding Properties at Run Time

Properties can be added dynamically:

```js
mark.bonus = 3000;
```

This property exists only on `mark`.

To add a property shared by all descendants, add it to the prototype:

```js
Employee.prototype.specialty = "none";
```

All objects inheriting from `Employee` now expose the `specialty` property.

---

## More Flexible Constructors

Constructors can accept arguments to initialize properties:

```js
function Employee(name, dept) {
    this.name = name || "";
    this.dept = dept || "general";
}
```

The logical OR idiom (`||`) is commonly used to assign default values.

To initialize inherited properties, constructors may explicitly invoke other constructors:

```js
function Engineer(name, projs, mach) {
    WorkerBee.call(this);
    this.name = name || "";
    this.dept = "engineering";
    this.projects = projs || [];
    this.machine = mach || "";
}
Engineer.prototype = new WorkerBee();
```

---

## Local vs. Inherited Values

If a property exists locally, it shadows any inherited property of the same name. To allow default values to be updated dynamically for all objects, define those defaults on the prototype rather than inside the constructor.

---

## Determining Prototype Relationships

ProtoScript does not provide a built-in `instanceof` operator. Prototype relationships can be tested manually:

```js
function instanceOf(object, constructor) {
    while (object != null) {
        if (object == constructor.prototype)
            return true;
        object = object.__proto__;
    }
    return false;
}
```

---

## Global State in Constructors

Constructors that modify global state (such as counters) must be designed carefully. Constructors may be invoked implicitly when establishing prototypes, which can lead to unintended side effects.

---

## No Multiple Inheritance

ProtoScript supports **single inheritance only**. An object has exactly one prototype. While a constructor may call multiple other constructors, this does not establish multiple prototype chains.

