![ProtoScript](../header.png)

# When One Example Would Have Changed Everything

## A Personal Retrospective

In the late 1990s, many developers (myself included) encountered JavaScript through the lens of *Java*. The name alone suggested classes, inheritance hierarchies, and object-oriented (OO) patterns that were already familiar—but JavaScript did not actually work that way. As a result, its true model remained obscure.

Looking back, it is striking how a **single, well-chosen example** could have clarified everything. A simple data structure—implemented cleanly and honestly in the prototype-based style—would have revealed what JavaScript was really about.

This paper presents such an example: a **singly linked list**, written in plain ES1-style JavaScript, without classes, without syntactic sugar, and without conceptual compromise.

---

## The Core Idea: Objects Linked by Delegation

Prototype-based object orientation is not about *types* or *inheritance trees*. It is about:

- Objects holding state
- Objects delegating behavior through prototypes
- Explicit construction and composition

A linked list is ideal here, because it is *itself* a structure built from linked objects.

---

## The Node: A Minimal Stateful Object

```js
function LinkedListNode(value) {
    this.value = value;
    this.next = null;
}
```

This constructor does exactly one thing: **create an object with state**.

There is no behavior here. No methods. No inheritance. Just data.

Key points:

- Each invocation of `LinkedListNode` **with `new`** produces a fresh object instance: `var node = new LinkedListNode(value);`

  The `new` operator is essential here: it allocates a new object, binds it to `this`, and links it to `LinkedListNode.prototype`. Without `new`, the constructor doesn’t create a fresh instance.
- `value` and `next` are *own properties*
- The node does not know about the list it belongs to

This strict separation is intentional.

---

## The List Object: State Without Behavior (Yet)

```js
function LinkedList() {
    this.head = null;
    this.tail = null;
    this.length = 0;
}
```

Again: pure state.

The list object tracks:

- `head`: first node
- `tail`: last node
- `length`: number of elements

No logic is embedded here. That comes next.

---

## Adding Behavior via the Prototype

Behavior is shared by *delegation*, not copied per instance.

```js
LinkedList.prototype.push = function (value) {
    var node = new LinkedListNode(value);
    if (!this.head) {
        this.head = node;
        this.tail = node;
    } else {
        this.tail.next = node;
        this.tail = node;
    }
    this.length++;
    return node;
};
```

What matters here is not the algorithm (which is straightforward), but the *mechanics*:

- `push` lives on `LinkedList.prototype`, not on individual list instances
- Every `LinkedList` instance delegates method lookup to this prototype object
- Any kind of value can be stored in the list (strings, numbers, arrays, or arbitrary objects)
- `new LinkedListNode(value)` explicitly allocates a node object and initializes its local state
- `this` is resolved dynamically at call time and always refers to the list instance on which `push` is invoked
- If the list is empty, the new node becomes both `head` and `tail`; otherwise, it is linked after the current tail and promoted as the new tail

There is **no class**. There is an object that other objects consult.

---

## Prepending: Same Structure, Different Traversal

```js
LinkedList.prototype.prepend = function (value) {
    var node = new LinkedListNode(value);
    if (!this.head) {
        this.head = node;
        this.tail = node;
    } else {
        node.next = this.head;
        this.head = node;
    }
    this.length++;
    return node;
};
```

The list remains consistent because:

- State lives on the instance
- Behavior lives on the prototype
- The algorithm manipulates *links*, not indexes

This reinforces an important lesson: **structure emerges from references, not from containers**.

---

## Searching by Traversal

```js
LinkedList.prototype.find = function (value) {
    var cur = this.head;
    while (cur) {
        if (cur.value === value) return cur;
        cur = cur.next;
    }
    return null;
};
```

Here we see object graphs in action:

- Each node references the next
- Traversal is explicit
- No abstraction hides the mechanics

This is prototype-based thinking at its clearest.

---

## Removing a Node: Local Reasoning Only

```js
LinkedList.prototype.remove = function (value) {
    var prev = null;
    var cur = this.head;
    while (cur) {
        if (cur.value === value) {
            if (prev) {
                prev.next = cur.next;
            } else {
                this.head = cur.next;
            }
            if (cur === this.tail) {
                this.tail = prev;
            }
            this.length--;
            return true;
        }
        prev = cur;
        cur = cur.next;
    }
    return false;
};
```

No global invariants. No superclass contracts.

Just:

- Local references
- Explicit mutation
- Clear ownership of state

This is *object-oriented programming without ceremony*.

---

## Observation, Not Encapsulation

```js
LinkedList.prototype.toArray = function () {
    var out = [];
    var cur = this.head;
    while (cur) {
        out.push(cur.value);
        cur = cur.next;
    }
    return out;
};
```

The list does not hide its internals behind layers of abstraction. Instead:

- Representation is stable
- Behavior is predictable
- Debugging is trivial

Nothing is implicit.

---

## A Concrete Usage Example in ProtoScript

To conclude, here is a minimal but complete usage example written in **ProtoScript**. It shows how the data structure is consumed from user code, without any special syntax or runtime magic.

```js
include "LinkedList.js";

var list = new LinkedList();
list.push("b");
list.push("c");
list.prepend("a");

var found = list.find("b");
Io.print((found ? "found b" : "missing b") + Io.EOL);

list.remove("a");
Io.print("len=" + list.length + Io.EOL);

var values = list.toArray();
Io.print(values.join(",") + Io.EOL);
```

Several important observations can be made:

- `include` includes a source file statically and executes it in the same global scope
- `LinkedList` is used as a plain constructor function
- Method calls (`push`, `prepend`, `find`, `remove`) rely entirely on prototype delegation
- The list exposes *behavior*, not iterators or hidden machinery
- State (`length`, links between nodes) remains fully observable and predictable

This example demonstrates that prototype-based OO scales naturally from definition to usage, without any change in mental model.

---

## Why This Example Matters

This small program demonstrates, in one place:

- Constructor functions as object factories
- Prototypes as shared behavior objects
- Delegation instead of inheritance
- Explicit object graphs
- No confusion between *class* and *instance*

Had this been shown early on, JavaScript would have been understood very differently.



## Final Thought

Prototype-based OO is not a weaker form of class-based OO.

It is **simpler**, **more explicit**, and **closer to how programs actually execute**.

This linked list is not just a data structure.

It is a conceptual Rosetta Stone.

