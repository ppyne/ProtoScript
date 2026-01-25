![ProtoScript](../header.png)

# ProtoScript — Object Creation with ES1

This documentation explains **the real object creation model of JavaScript as it exists in ECMAScript 1**, deliberately restricted to the **minimal core of the language**.

The chosen framework is **ProtoScript**, but this document stays strictly within the **ECMAScript 1 (1997)** mindset:

- no `class`
- no ES5+ helpers (including `Object.create`)
- only foundational mechanisms: functions, objects, prototypes, delegation

The goal is not convenience, but **precise understanding of the conceptual engine**.

---

## 1. Functions as object constructors (ES1)

In JavaScript, **a function can act as an object constructor**. This mechanism exists **since ES1**.

A function becomes a constructor **only when it is invoked with the `new` operator**.

### Minimal example

```js
function Person(name) {
    this.name = name;
}

var p = new Person("Alice");
```

### What `new` actually does

The call:

```js
new Person("Alice")
```

is conceptually equivalent to:

1. Create a new empty object
2. Link this object internally to `Person.prototype`
3. Call `Person` with:
   - `this` bound to the new object
4. Implicitly return the object (unless the function explicitly returns another object)

### Equivalent pseudo-code

```js
var obj = {};
obj.[[Prototype]] = Person.prototype;
Person.call(obj, "Alice");
return obj;
```

⚠️ `[[Prototype]]` is an internal language mechanism (not directly accessible in ES1).

---

## 2. The role of `prototype`

Every function has a property:

```js
Person.prototype
```

This property:

- is an **ordinary object**
- acts as a **delegation target** for objects created with `new Person()`

### Adding shared methods

```js
Person.prototype.sayHello = function () {
    return "Hello, my name is " + this.name;
};

p.sayHello(); // "Hello, my name is Alice"
```

### Delegation chain

```
p → Person.prototype → Object.prototype → null
```

---

## 3. Key points to understand

- `prototype` **is not the prototype of the object**
- `prototype` is **the prototype of objects created by the function**
- Objects **do not copy** methods: they delegate to them
- Multiple instances share the same `prototype`

```js
var p1 = new Person("Alice");
var p2 = new Person("Bob");

p1.sayHello === p2.sayHello; // true
```

---

## 4. Common conceptual mistakes

❌ Confusing `obj.prototype` (does not exist)

❌ Assuming inheritance is based on copying

❌ Believing `new` creates a class

---

## 5. Takeaway

- The core of the JavaScript object model is built on:
  - **functions**
  - **prototype-based delegation**
  - the internal `[[Prototype]]` link

The following sections will cover:

- delegation without constructors (and why ES1 cannot do it cleanly)
- property resolution step by step
- method overriding
- comparison with `class` syntax (conceptually only)

---

## 6. The inheritance link (prototype linkage)

In ES1, there is **no explicit syntax for inheritance**. What exists instead is a **prototype linkage mechanism**, entirely based on object delegation.

### The fundamental rule

> An object does not inherit from another object by copying. It delegates property lookup to another object through an internal link.

This internal link is called:

```
[[Prototype]]
```

It exists **in ES1**, but it is **not directly accessible** at the language level.

---

## 6.1 Constructor-based inheritance (ES1 style)

In ES1, the *only* way to establish an inheritance relationship is via **constructor functions** and their `prototype` property.

### Example&#x20;

```js
function Device() {}

Device.prototype.getStatus = function () {
    return "idle";
};

function Printer(model) {
    this.model = model;
}

Printer.prototype = new Device();
Printer.prototype.constructor = Printer;

Printer.prototype.getStatus = function () {
    return "printing";
};

var p = new Printer("LaserJet");
p.getStatus(); // "printing"
```

### What actually happens

- `Printer.prototype` is assigned an object created by `new Device()`
- That object delegates to `Device.prototype`
- Instances of `Printer` delegate to `Printer.prototype`

Delegation chain:

```
p → Printer.prototype → Device.prototype → Object.prototype → null
```

---

## 6.2 Why this works

Because property resolution in JavaScript follows this algorithm:

1. Look for the property on the object itself
2. If not found, follow `[[Prototype]]`
3. Repeat until `null` is reached

No copying ever occurs.

---

## 6.3 Structural limitations of ES1 inheritance

This ES1-style inheritance has **important limitations**:

- Constructors are executed only once (when setting `Printer.prototype`)
- Shared state can easily leak through the prototype
- There is no clean way to call a "super" constructor
- Inheritance and instantiation are tightly coupled

These are **not bugs** — they are consequences of the original design.

---

## 6.4 Key takeaways

- ES1 inheritance is **prototype chaining**, not class inheritance
- The `prototype` object is the only inheritance hook
- `new` serves two roles at once:
  - object creation
  - prototype linkage

ProtoScript embraces this model explicitly, rather than hiding it behind abstractions.

---

## 7. How to represent prototype-based designs graphically

Class diagrams are **not suitable** for prototype-based systems. They encode assumptions that are **false in ES1**:

- fixed classes
- static inheritance trees
- instantiation separated from inheritance

ProtoScript requires **different visual representations**.

---

## 7.1 Prototype graph (recommended)

The correct mental and graphical model is a **directed graph of objects**, not a class hierarchy.

Each node is an **object**. Each arrow represents a **delegation link**.

Example:

```
[p instance]
      |
      v
[Printer.prototype]
      |
      v
[Device.prototype]
      |
      v
[Object.prototype]
      |
      v
    null
```

Key properties:

- arrows mean *delegates to*, not *inherits from*
- no notion of class
- runtime structure, not a design-time abstraction

---

## 7.2 Prototype + own properties view

Each object should be drawn as a **box with two zones**:

```
+---------------------+
| Printer instance    |
|---------------------|
| model: "LaserJet"   |   ← own properties
+---------------------+
           |
           v
+---------------------+
| Printer.prototype   |
|---------------------|
| getStatus()         |   ← shared behavior
+---------------------+
```

This makes explicit:

- what is owned
- what is delegated

---

## 7.3 Why UML class diagrams fail here

UML class diagrams assume:

- instances are created *from* classes
- inheritance is static and declarative
- methods belong to classes

None of this is true in ES1.

In ProtoScript:

- objects are created first
- delegation is dynamic
- behavior lives in objects, not classes

Using class diagrams **introduces conceptual bugs**.

---

## 7.4 Recommended notation for ProtoScript

Use:

- **object graphs**
- **prototype chains**
- **delegation arrows**

Avoid:

- class boxes
- inheritance triangles
- abstract base classes

---

## 7.5 Takeaway

> Prototype-based design is best represented as a **runtime object graph**, not as a compile-time class hierarchy.

ProtoScript documentation and reasoning should always reflect this reality.

---

## 8. Property lookup: step-by-step resolution (ES1)

Understanding **property lookup** is essential to understanding JavaScript. This mechanism exists **unchanged since ES1** and defines how delegation actually works at runtime.

---

## 8.1 The lookup algorithm

When evaluating:

```js
obj.prop
```

JavaScript performs the following steps:

1. Check whether `obj` has its **own property** named `prop`
2. If found, return its value
3. Otherwise, follow the internal `[[Prototype]]` link
4. Repeat the process on the prototype object
5. Stop when either:
   - the property is found
   - `null` is reached (result is `undefined`)

No copying, caching, or precomputation occurs.

---

## 8.2 Concrete example

```js
function Device() {}

Device.prototype.status = "idle";

function Printer() {}
Printer.prototype = new Device();

var p = new Printer();

p.status;
```

Resolution trace:

1. `p` → no own property `status`
2. `Printer.prototype` → no own property `status`
3. `Device.prototype` → property found: `"idle"`
4. Return value

---

## 8.3 Shadowing (override)

If a property exists at multiple levels, the **closest one wins**.

```js
Device.prototype.status = "idle";
Printer.prototype.status = "printing";

p.status; // "printing"
```

- The prototype chain is **not modified**
- Lookup simply stops earlier

This is called **property shadowing**, not overriding in the class sense.

---

## 8.4 Assignment vs lookup (critical distinction)

```js
p.status = "paused";
```

This does **not** modify the prototype. Instead:

- A new **own property** `status` is created on `p`
- Prototype objects remain unchanged

After assignment:

```
p (status = "paused")
 → Printer.prototype (status = "printing")
 → Device.prototype (status = "idle")
```

---

## 8.5 Deletion and fallback

```js
delete p.status;
p.status; // "printing"
```

- `delete` removes the **own property only**
- Lookup resumes through the prototype chain

---

## 8.6 Key consequences

- Prototype chains are **live**
- Changes to prototypes affect all delegating objects
- Objects never "inherit values", only lookup behavior

This explains both the power and the danger of prototype-based design.

---

## 8.7 Takeaway

> Property access in JavaScript is a **runtime search process**, not a static inheritance mechanism.

ProtoScript relies on this fact explicitly and never hides it behind abstractions.

---

## 9. Why deep prototype chains are dangerous (ES1)

Deep prototype chains are **technically valid** in ES1, but they are **structurally fragile** and often lead to subtle bugs.

This is not a matter of style or preference — it is a consequence of how **property lookup actually works**.

---

## 9.1 Lookup cost grows with depth

Property access time is proportional to the **length of the prototype chain**.

```js
obj.prop
```

Resolution requires:

- checking the object
- then each prototype, one by one

In deep chains:

- lookup becomes harder to reason about
- performance becomes unpredictable
- debugging requires traversing multiple objects mentally

In ES1, there is **no tooling** to inspect this safely.

---

## 9.2 Hidden coupling through shared prototypes

Every object in the chain is **shared state**.

```js
Base.prototype.flag = true;
```

This affects **all objects delegating to `Base.prototype`**, including:

- objects you did not intend to modify
- objects created earlier
- objects in other subsystems

The deeper the chain, the harder it is to identify:

- where a value comes from
- who depends on it

---

## 9.3 Accidental shadowing

With deep chains, adding a property can silently change behavior:

```js
obj.prop = 1;
```

This may:

- shadow a property defined three prototypes above
- break assumptions elsewhere
- be invisible during code review

The danger increases with depth.

---

## 9.4 Constructor side effects multiply

In ES1-style inheritance:

```js
Child.prototype = new Parent();
```

- constructors execute during *prototype setup*
- side effects happen **once**, but affect all instances

With multiple levels:

- constructor logic leaks into the inheritance graph
- ordering becomes critical
- reasoning becomes non-local

---

## 9.5 No safe "super" semantics

In ES1:

- there is no `super`
- calling parent behavior requires manual indirection

In deep chains:

- calling the "right" ancestor becomes unclear
- refactoring breaks method resolution silently

---

## 9.6 Debugging complexity

When a property behaves unexpectedly, you must ask:

- Is it an own property?
- Which prototype defines it?
- Was it shadowed?
- Was the prototype modified later?

With deep chains, answering these questions is slow and error-prone.

---

## 9.7 Practical rule for ProtoScript

> **Prefer shallow prototype chains (depth ≤ 2).**

Good pattern:

```
instance → behavior object → Object.prototype
```

Avoid:

```
instance → A → B → C → D → Object.prototype
```

---

## 9.8 Takeaway

Deep prototype chains:

- increase cognitive load
- hide dependencies
- amplify side effects
- make systems brittle

ProtoScript deliberately favors **shallow delegation and explicit composition** over deep inheritance structures.

---

## 10. Composition vs delegation in ES1

In ES1, **composition and delegation are often confused**, but they solve **different problems** and have very different consequences.

Understanding this distinction is essential to designing robust ProtoScript systems.

---

## 10.1 Delegation (prototype-based reuse)

Delegation is the **native reuse mechanism** of JavaScript. It is implicit and driven by the `[[Prototype]]` lookup.

```js
function Logger() {}
Logger.prototype.log = function (msg) {
    /* logging */
};

function Service() {}
Service.prototype = new Logger();

var s = new Service();
s.log("hello");
```

Characteristics:

- reuse via prototype chain
- behavior is **shared**
- changes propagate automatically
- coupling is implicit

Delegation answers the question:

> “Where should this object look when it does not know how to respond?”

---

## 10.2 Composition (explicit wiring)

Composition is **explicit object collaboration**. Objects hold references to other objects and forward calls intentionally.

```js
function Logger() {
    this.log = function (msg) {
        /* logging */
    };
}

function Service(logger) {
    this.logger = logger;
    this.doWork = function () {
        this.logger.log("working");
    };
}

var s = new Service(new Logger());
```

Characteristics:

- reuse via object references
- behavior is **not shared implicitly**
- dependencies are visible
- easier to reason about

Composition answers the question:

> “Who does this object collaborate with?”

---

## 10.3 Key differences

| Aspect        | Delegation             | Composition         |
| ------------- | ---------------------- | ------------------- |
| Mechanism     | `[[Prototype]]` lookup | Explicit references |
| Coupling      | Implicit               | Explicit            |
| State sharing | Yes (via prototypes)   | No (unless chosen)  |
| Flexibility   | Medium                 | High                |
| Debugging     | Harder                 | Easier              |

---

## 10.4 When delegation is appropriate

Delegation works best for:

- shared, stateless behavior
- method reuse
- polymorphic dispatch

Bad use cases:

- shared mutable state
- deep inheritance trees

---

## 10.5 When composition is safer

Composition is preferable when:

- state must be isolated
- dependencies must be explicit
- behavior may change dynamically
- testing and reasoning matter

In ES1, composition often relies on **closures** rather than prototypes.

---

## 10.6 ProtoScript design rule

> **Use delegation for behavior reuse.** **Use composition for state and collaboration.**

Mixing both consciously is fine. Mixing them accidentally is dangerous.

---

## 10.7 Takeaway

Delegation is what JavaScript gives you. Composition is what disciplined design requires.

ProtoScript embraces delegation **without abusing it**, and uses composition to keep systems understandable.

---

## 11. Closures vs prototypes: clear separation of roles

Closures and prototypes are **orthogonal mechanisms** in ES1. They solve **different problems**, and confusing their roles leads to fragile designs.

ProtoScript relies on a **strict separation of responsibilities** between them.

---

## 11.1 What closures are for (ES1)

A closure is created when a function captures variables from its lexical scope.

```js
function makeCounter() {
    var count = 0;
    return function () {
        count = count + 1;
        return count;
    };
}

var counter = makeCounter();
counter(); // 1
counter(); // 2
```

Closures provide:

- true **private state**
- lexical encapsulation
- controlled access to data

Closures answer the question:

> “What state must be protected and invisible from the outside?”

---

## 11.2 What prototypes are for (ES1)

Prototypes exist to provide **shared behavior through delegation**.

```js
function Counter() {
    this.value = 0;
}

Counter.prototype.inc = function () {
    this.value = this.value + 1;
};
```

Prototypes provide:

- method reuse
- polymorphic dispatch
- memory efficiency

Prototypes answer the question:

> “Where should this object look for behavior it does not own?”

---

## 11.3 The critical difference: state ownership

| Mechanism | State visibility | Sharing      |
| --------- | ---------------- | ------------ |
| Closure   | Private          | Per instance |
| Prototype | Public           | Shared       |

- Closure state is **never shared accidentally**
- Prototype state is **always shared**

This difference is fundamental.

---

## 11.4 The most common design mistake

Putting mutable state on prototypes:

```js
Counter.prototype.value = 0; // dangerous
```

This creates:

- shared mutable state
- action at a distance
- non-local bugs

This mistake becomes catastrophic in deep prototype chains.

---

## 11.5 Correct separation pattern (ES1)

Use:

- **closures for state**
- **prototypes for behavior**

Example:

```js
function Counter() {
    var value = 0; // private

    this.inc = function () {
        value = value + 1;
        return value;
    };
}
```

Here:

- state is private
- behavior is explicit
- no shared mutable data

---

## 11.6 Trade-offs

This pattern:

- uses more memory per instance
- but dramatically improves correctness

ProtoScript deliberately prefers:

> correctness and clarity over micro-optimizations

---

## 11.7 Takeaway

> **Closures protect state.** **Prototypes share behavior.**

Keeping these roles separate is one of the most important design rules in ProtoScript.

---

## 12. Why `new` is the most misleading keyword in JavaScript (ES1)

The keyword `new` is one of the main sources of misunderstanding in JavaScript. It suggests **class-based instantiation**, while JavaScript is **prototype-based**.

In ES1, `new` does not create a class instance. It performs a **mechanical sequence of operations** that are easy to misuse and hard to see.

---

## 12.1 What `new` appears to mean (but does not)

In class-based languages, `new` usually means:

- allocate an instance
- initialize it from a class
- attach methods defined by the class

This mental model is **wrong in JavaScript**.

---

## 12.2 What `new` actually does (ES1 reality)

When evaluating:

```js
new F(a, b)
```

JavaScript performs the following steps:

1. Create a new empty object
2. Set its internal `[[Prototype]]` to `F.prototype`
3. Call `F` with `this` bound to that object
4. If `F` returns an object, return it
5. Otherwise, return the newly created object

`new` does **not**:

- enforce structure
- enforce invariants
- guarantee initialization

---

## 12.3 The dual role problem

The constructor function `F` plays **two unrelated roles at once**:

1. Initializer (via side effects on `this`)
2. Prototype provider (via `F.prototype`)

This coupling is accidental and fragile.

Changing one role often breaks the other.

---

## 12.4 Silent failure modes

Common mistakes:

```js
var o = F();   // forgot `new`
```

Consequences:

- `this` is bound to the global object (non-strict)
- or `undefined` (strict mode)
- properties leak or cause runtime errors

The language does not protect you.

---

## 12.5 Constructors are not constructors

In ES1:

- functions are just functions
- nothing marks them as constructors
- calling them with or without `new` changes semantics

There is no syntactic or semantic safeguard.

---

## 12.6 Inheritance becomes entangled

Because `new` is also used to build prototype chains:

```js
Child.prototype = new Parent();
```

- constructors run during inheritance setup
- side effects leak into prototypes
- inheritance and instantiation are mixed

This is a design trap, not a feature.

---

## 12.7 Why this keyword survived

`new` survived because:

- it was familiar to class-based programmers
- it made JavaScript look simpler than it was
- removing it would break compatibility

But it has been misleading **since ES1**.

---

## 12.8 ProtoScript position

ProtoScript treats `new` as:

- a low-level primitive
- not a design abstraction

Its use is:

- explicit
- limited
- carefully documented

Whenever possible, ProtoScript favors:

- explicit object creation
- clear delegation
- closures for state

---

## 12.9 Takeaway

> `new` looks simple, but hides multiple mechanisms. Understanding JavaScript requires mentally expanding `new` every time you see it.

This is why `new` is the most misleading keyword in the language.

---

## 13. Shared state: failure modes and avoidance patterns (ES1)

Shared state is one of the **most common sources of bugs** in prototype-based systems. In ES1, shared state arises **naturally and silently** through prototypes.

Understanding how it appears — and how to avoid it — is essential in ProtoScript.

---

## 13.1 What shared state means in ES1

A state is *shared* when **multiple objects access and mutate the same data**.

In ES1, this typically happens when **mutable properties are placed on prototypes**.

```js
function Buffer() {}

Buffer.prototype.data = [];
```

Every object delegating to `Buffer.prototype` now shares the **same array**.

---

## 13.2 Typical failure modes

### 1) Accidental global coupling

```js
b1.data.push(1);
b2.data.push(2);
```

Both `b1` and `b2` observe:

```js
[1, 2]
```

Objects interfere without explicit coordination.

---

### 2) Temporal bugs

Behavior depends on **creation order** or **execution history**.

```js
Buffer.prototype.data.length = 0;
```

This resets state for **all existing and future instances**.

---

### 3) Action at a distance

A change in one subsystem alters behavior in another, unrelated subsystem.

Debugging requires global reasoning.

---

### 4) Broken assumptions during refactoring

Moving a property from instance to prototype (or the reverse) silently changes semantics.

No syntax error warns you.

---

## 13.3 Why prototypes amplify the problem

Because:

- prototypes are **shared by design**
- lookup is **dynamic and live**
- assignment creates shadowing instead of modification

The resulting behavior is often non-intuitive.

---

## 13.4 Avoidance pattern #1: instance-owned state

Always put mutable state on the **instance**, not the prototype.

```js
function Buffer() {
    this.data = [];
}
```

Each object now owns its state explicitly.

---

## 13.5 Avoidance pattern #2: closures for private state

When state must not be observable or mutable externally:

```js
function makeBuffer() {
    var data = [];
    return {
        push: function (x) { data.push(x); },
        read: function () { return data.slice(); }
    };
}
```

This provides:

- isolation
- controlled access
- zero accidental sharing

---

## 13.6 Avoidance pattern #3: immutable shared data

If data is placed on a prototype, it must be:

- immutable
- treated as read-only

```js
Config.prototype.defaults = {
    timeout: 1000
};
```

Mutation here is a design error.

---

## 13.7 ProtoScript rule of thumb

> **Shared behavior is fine.** **Shared mutable state is almost always a bug.**

ProtoScript assumes that any prototype property is shared unless proven otherwise.

---

## 13.8 Takeaway

Shared state failures:

- are silent
- scale with system size
- are hard to debug

ProtoScript avoids them by:

- keeping state local
- using closures deliberately
- treating prototypes as behavior-only objects

---

## 14. Minimal object patterns in ES1

This section presents **minimal, idiomatic object patterns** that work reliably in ES1. They avoid hidden mechanisms, deep inheritance, and unnecessary abstraction.

These patterns are the **building blocks** of ProtoScript-style design.

---

## 14.1 Plain object literal (data object)

Use when:

- representing structured data
- no behavior or very local behavior is needed

```js
var point = {
    x: 10,
    y: 20
};
```

Characteristics:

- no prototype manipulation
- explicit structure
- easy to inspect

---

## 14.2 Constructor + prototype (behavior sharing)

Use when:

- many objects share the same behavior
- state is instance-owned

```js
function Stack() {
    this.items = [];
}

Stack.prototype.push = function (x) {
    this.items.push(x);
};

Stack.prototype.pop = function () {
    return this.items.pop();
};
```

Characteristics:

- shared methods
- per-instance state
- shallow prototype chain

---

## 14.3 Closure-based factory (encapsulation-first)

Use when:

- state must be private
- invariants must be enforced
- sharing behavior is secondary

```js
function makeCounter() {
    var value = 0;
    return {
        inc: function () { value = value + 1; return value; },
        read: function () { return value; }
    };
}
```

Characteristics:

- true encapsulation
- no shared mutable state
- higher per-instance cost

---

## 14.4 Delegation-only object (behavior object)

Use when:

- defining reusable behavior blocks
- no state is required

```js
var printable = {
    print: function () {
        /* printing logic */
    }
};
```

Characteristics:

- stateless
- safe to share
- designed to be delegated to

---

## 14.5 Composition via explicit wiring

Use when:

- objects collaborate
- dependencies must be visible

```js
function Service(logger) {
    this.logger = logger;
}

Service.prototype.run = function () {
    this.logger.log("run");
};
```

Characteristics:

- explicit dependencies
- no hidden lookup
- easy to test

---

## 14.6 Anti-pattern: deep inheritance tree

Avoid:

```js
A → B → C → D
```

Reasons:

- hidden coupling
- fragile behavior
- difficult reasoning

---

## 14.7 Selection guide

| Need              | Pattern                 |
| ----------------- | ----------------------- |
| Plain data        | Object literal          |
| Shared behavior   | Constructor + prototype |
| Encapsulation     | Closure factory         |
| Reusable behavior | Stateless delegate      |
| Collaboration     | Composition             |

---

## 14.8 Takeaway

> ES1 does not lack expressiveness. It requires **discipline and minimalism**.

ProtoScript favors a small set of simple patterns used consistently, rather than large abstraction hierarchies.

---

## 15. ProtoScript design rules (concise summary)

This chapter summarizes the **core design rules** of ProtoScript. They are derived directly from **ES1 semantics**, not from later abstractions or stylistic trends.

These rules are intentionally **few, strict, and composable**.

---

## Rule 1 — Think in objects, not classes

There are **no classes in ES1**. There are only objects and delegation.

- Do not design hierarchies
- Do not model taxonomies
- Do not search for “base classes”

Design **object relationships**, not type trees.

---

## Rule 2 — Delegation is behavior lookup, nothing more

Prototypes exist to:

- share behavior
- provide fallback methods

They do **not**:

- define identity
- define structure
- define ownership

Treat every prototype as a **behavior table**.

---

## Rule 3 — Never put mutable state on prototypes

Mutable prototype state is **shared by definition**.

Allowed on prototypes:

- functions
- constants
- read-only configuration

Everything else belongs:

- on the instance
- or inside a closure

---

## Rule 4 — Use closures for encapsulation

If state must be:

- private
- protected
- invariant

Then it must live in a **closure**, not on `this`.

There is no alternative in ES1.

---

## Rule 5 — Prefer shallow prototype chains

Deep chains multiply:

- lookup cost
- coupling
- debugging effort

Preferred shape:

```
instance → behavior → Object.prototype
```

Avoid inheritance depth greater than 2.

---

## Rule 6 — Composition over inheritance

When objects collaborate:

- pass references explicitly
- wire dependencies visibly

Do not rely on prototype lookup for coordination.

---

## Rule 7 — Treat `new` as a low-level primitive

`new` is:

- not a class instantiator
- not a safety mechanism

Every use of `new` should be justifiable by:

- behavior sharing needs
- memory considerations

Never use `new` by habit.

---

## Rule 8 — Prefer explicitness over cleverness

Avoid:

- implicit shadowing
- runtime mutation of prototypes
- meta-programming tricks

ES1 rewards clarity, not cleverness.

---

## Rule 9 — Optimize correctness before reuse

Code reuse is meaningless if behavior is unclear.

It is better to:

- duplicate simple code
- than to share complex behavior incorrectly

---

## Rule 10 — Remember the mental model

> JavaScript ES1 is a **runtime delegation system**, not a compile-time type system.

Design accordingly.

---

## Final takeaway

ProtoScript is not about nostalgia. It is about **removing illusions** and programming JavaScript as the language actually works.

Master ES1, and every later version becomes simpler.

---

## 16. Anti-patterns inherited from class-based thinking

Many JavaScript design problems do not come from the language itself, but from **mental models imported from class-based languages**.

In ES1, these models are not just inappropriate — they actively produce bugs.

---

## 16.1 Treating constructors as classes

**Anti-pattern**:

```js
function User() {}
```

Thinking:

> “`User` is a class.”

Reality:

- `User` is just a function
- it becomes a constructor **only** when called with `new`
- nothing enforces structure or invariants

This mindset leads to:

- false assumptions about safety
- misuse of `new`
- fragile APIs

---

## 16.2 Designing deep inheritance hierarchies

**Anti-pattern**:

```
Entity → User → Admin → SuperAdmin
```

Problems:

- hidden coupling
- unpredictable lookup
- brittle refactoring

ES1 has **no tooling** to support this safely.

---

## 16.3 Using prototypes as state containers

**Anti-pattern**:

```js
Thing.prototype.items = [];
```

This imports the idea that:

> “Instances have their own fields defined by the class.”

In ES1, this creates **shared mutable state**.

---

## 16.4 Expecting encapsulation from `this`

**Anti-pattern**:

```js
this.secret = 42;
```

Assuming this is private is a class-based reflex.

In ES1:

- everything on `this` is public
- nothing prevents external mutation

Only closures provide real encapsulation.

---

## 16.5 Expecting `super` semantics

**Anti-pattern**:

> “Call the parent method.”

In ES1:

- there is no `super`
- prototype lookup does not encode intent
- calling ancestors is manual and fragile

Trying to simulate `super` leads to:

- brittle code
- tight coupling

---

## 16.6 Overvaluing reuse through inheritance

Class-based thinking encourages:

> “Reuse by inheritance.”

In ES1, this results in:

- over-shared behavior
- over-shared assumptions
- accidental dependencies

Composition is usually safer.

---

## 16.7 Believing structure exists before runtime

**Anti-pattern**:

> “The object model is defined at design time.”

In ES1:

- objects are shaped at runtime
- prototypes are mutable
- structure is dynamic

Designs that ignore this reality break.

---

## 16.8 ProtoScript corrective mindset

ProtoScript replaces class-based assumptions with:

- object-first thinking
- explicit delegation
- explicit composition
- closures for invariants

---

## 16.9 Takeaway

> Most JavaScript anti-patterns are not JavaScript problems. They are **class-based thinking problems**.

Dropping those assumptions is a prerequisite to mastering ES1 and ProtoScript.

---

## 17. How to read legacy JavaScript correctly (ES1 mindset)

Reading legacy JavaScript requires **unlearning modern abstractions** and adopting the **mental model of ES1**. Most legacy code was written **before ES5 discipline** and **before ES6 syntax**, but it is not random.

This chapter provides a methodical way to read it correctly.

---

## 17.1 Assume ES1 semantics first

When reading old JavaScript, assume:

- no classes
- no block scope
- no modules
- no encapsulation by default

Every function, object, and prototype must be interpreted **literally**, not symbolically.

---

## 17.2 Expand `new` mentally

Whenever you see:

```js
new F()
```

Mentally rewrite it as:

```js
var o = {};
o.[[Prototype]] = F.prototype;
F.call(o);
return o;
```

This immediately reveals:

- side effects
- prototype linkage
- hidden coupling

---

## 17.3 Track state ownership explicitly

For every property you encounter, ask:

- Is this an own property?
- Is it on the prototype?
- Is it shared?

If a mutable value lives on a prototype, treat it as a **red flag**.

---

## 17.4 Read prototypes as behavior tables

Ignore names like `Base`, `Manager`, or `Controller`.

Instead, read:

```js
X.prototype.method
```

as:

> “If an object does not know how to respond, it may delegate here.”

This removes false class assumptions.

---

## 17.5 Look for closure boundaries

Closures are often the **only encapsulation mechanism** in legacy code.

Identify:

- factory functions
- IIFEs
- returned inner functions

These usually protect critical state.

---

## 17.6 Be suspicious of inheritance chains

When you see:

```js
B.prototype = new A();
```

Check immediately:

- does `A` have side effects?
- does it initialize state?
- is that state shared?

Assume fragility until proven otherwise.

---

## 17.7 Do not trust naming conventions

Names like:

- `Class`
- `Interface`
- `Abstract`

have **no semantic meaning** in ES1.

Only behavior and delegation matter.

---

## 17.8 Read mutations as global operations

Modifying:

```js
X.prototype.foo = ...
```

is a **global behavioral change**.

Search for:

- when it happens
- who depends on it

Order matters.

---

## 17.9 Prefer reasoning over refactoring

When first reading legacy code:

- do not refactor
- do not "modernize"
- do not add `class`

First, understand the **runtime object graph**.

---

## 17.10 Takeaway

> Legacy JavaScript is not badly written modern JavaScript. It is JavaScript written with a different mental model.

Reading it correctly requires restoring that model.

---

## 18. Why ES1 still matters today

ECMAScript 1 is often seen as an obsolete historical artifact. This view is incorrect.

ES1 still matters because **the core semantics of JavaScript have never changed**. Everything added later was built *on top of* these foundations, not instead of them.

---

## 18.1 The object model is still ES1

Modern JavaScript still relies on:

- objects
- functions
- prototype delegation
- runtime property lookup

`class`, `extends`, and `super` are **syntax layers**, not semantic changes.

Understanding ES1 means understanding what those features expand to.

---

## 18.2 ES1 explains modern pitfalls

Many modern JavaScript bugs are ES1 bugs in disguise:

- accidental shared state
- incorrect assumptions about `this`
- misuse of inheritance
- overconfidence in abstractions

Without ES1 knowledge, these issues appear "mysterious".

With ES1 knowledge, they are predictable.

---

## 18.3 Performance and correctness still depend on ES1 rules

Engines optimize based on:

- object shapes
- prototype stability
- predictable lookup paths

These optimizations are constrained by **ES1 semantics**.

Code that fights the ES1 model:

- is harder to optimize
- is harder to reason about
- breaks in subtle ways

---

## 18.4 Tooling does not replace understanding

Modern tooling:

- transpilers
- linters
- type systems

can hide ES1 complexity, but they **cannot remove it**.

At runtime, the ES1 model always applies.

---

## 18.5 ES1 is the lowest common denominator

All JavaScript environments must support:

- ES1-style objects
- ES1-style functions
- ES1-style delegation

This makes ES1:

- portable
- durable
- conceptually stable

---

## 18.6 ProtoScript’s position

ProtoScript does not reject modern JavaScript.

It asserts that:

- mastery comes from the bottom up
- abstractions are optional
- fundamentals are permanent

ES1 is not a limitation. It is a **stable conceptual core**.

---

## 18.7 Final takeaway

> JavaScript changes in syntax. JavaScript does not change in nature.

ES1 still matters today because it defines that nature.

Understanding ES1 means understanding JavaScript — past, present, and future.
