# ProtoScript Garbage Collector Specification (v1)

## 1. Goals

1. Ensure **memory safety**: no systematic leaks caused by the object / closure / environment model, no double-free, no use-after-free inside the VM.
2. Guarantee **cycle collection** (mutually-referencing objects).
3. Enforce **explicit resource semantics**: the GC must never implicitly close external resources.
4. Provide **observability and testability** to objectively demonstrate correctness.

## 2. Memory Management Scope

### 2.1 GC-managed objects
The GC manages at minimum:

- Objects (maps, dictionaries, prototypes, properties)
- Strings (heap-allocated, referenced by ProtoScript values)
- Functions and closures (code/bytecode + captured environment)
- Lexical environments / scopes (heap-based activation records when applicable)
- Arrays / lists (if exposed by the language)
- Any heap structure reachable from a ProtoScript value

> AST nodes may remain outside the GC scope (managed by `ps_ast_free`) as long as they are not directly reachable as ProtoScript values. If ASTs become reachable, they must either be GC-managed or wrapped by a GC-managed object.

### 2.2 Non-GC-managed objects

- Purely internal native memory not exposed to the language (temporary buffers, private caches)
- External resources (file descriptors, sockets, OS handles)

These must be managed explicitly and never implicitly finalized by the GC.

## 3. Value Model and Invariants

### 3.1 Values and references
Any value that can reference a GC-managed object must:

- Either store a direct pointer to a GC cell
- Or store a stable handle (ID / index) resolved to a GC cell

**Invariant:** A GC cell must never be freed while reachable from the root set.

### 3.2 Write barriers

Version v1 does not require incremental or generational GC. If introduced later, all heap mutations must be protected by a write barrier.

## 4. GC Type and Algorithm

### 4.1 GC type

- **Precise GC** (exact): the runtime knows exactly where all references are
- No conservative scanning of the C stack (to avoid false positives and nondeterminism)

### 4.2 Algorithm (v1)

- Stop-the-world **mark-and-sweep**
- Mark phase: DFS or explicit mark stack
- Sweep phase: linear traversal of allocated cells, freeing unmarked ones

### 4.3 Compaction

- No compaction in v1
- Optional future extension (requires handles or indirection)

## 5. Memory Representation

Each GC-managed allocation begins with a header containing:

- Mark bit
- Type tag (STRING, OBJECT, FUNCTION, ENV, ARRAY, …)
- Optional size
- Next / previous pointers for GC lists
- Flags (e.g. pinned, debug-only)

**Requirements:**

- Allocation and deallocation must be amortized O(1)
- Freed cells must not remain reachable through any valid value

## 6. Root Set Definition

The GC must mark from the following roots:

1. VM value stack (all slots containing values)
2. VM registers (accumulator, this, return value, etc.)
3. Global variables and intrinsic objects
4. Call frames and lexical environment chains
5. Function arguments (including aliasing mechanisms if implemented)
6. Constant tables (string pools, literal caches)
7. Native handles retained by builtins (explicitly registered as roots)

### 6.1 Explicit root stack

The runtime must provide an explicit rooting mechanism:

- Frame-based scanning of VM slots, or
- An internal API such as `gc_root_push(Value*) / gc_root_pop()`

**Requirement:** No GC-managed reference may exist temporarily in native code without being visible to the GC before any allocation that could trigger a collection.

## 7. GC Triggers

### 7.1 Automatic GC

A collection is triggered when one or more of the following is true:

- Allocated bytes since last GC exceed a threshold
- Number of allocations since last GC exceeds a threshold

Thresholds may be adaptive:

- After GC: `threshold = max(min_threshold, live_bytes * growth_factor)`

### 7.2 Manual GC

Expose a standard API:

- `Gc.collect()` — forces a full collection

**Semantics:** Manual GC must not change observable language behavior, except for timing and memory usage.

## 8. External Resources (I/O Handles)

**Strict rule:** The GC must never implicitly close external resources.

- File handles, sockets, and OS resources must be closed explicitly by user code
- Objects wrapping I/O resources may be collected, but the underlying resource remains open unless explicitly closed

Recommended:

- Maintain a native registry of open handles
- Provide debug warnings for leaked (non-closed) handles, without auto-closing them

## 9. Finalizers

Version v1:

- No language-level finalizers
- No user-visible destructors

Rationale: ordering issues, resurrection hazards, reentrancy, exception safety.

Optional future extension:

- Native-only finalizers
- No access to high-level runtime
- No object resurrection

## 10. Cycle Collection

The GC must correctly collect:

- Object–object cycles
- Prototype chains with cycles
- Closure ↔ environment cycles

Any cycle not reachable from the root set must be reclaimed.

## 11. Language Interaction Constraints

Features such as:

- `eval`
- `with`
- `catch (e)` scopes
- `arguments` aliasing

May create complex environment graphs. All runtime environments that are reachable must be GC-managed and traced precisely.

## 12. GC Introspection API

Expose an internal (or test-visible) module `Gc`:

- `Gc.collect()`
- `Gc.stats()` → object containing:
  - total allocated bytes
  - live bytes after last GC
  - number of collections
  - freed objects in last GC
  - current thresholds
- Optional debug helpers:
  - `Gc.countsByType()`
  - `Gc.enableStress(true)` — GC on every allocation

**Requirement:** The API must allow objective proof of cycle collection via observable counters.

## 13. Performance Constraints (v1)

- Stop-the-world GC
- Mark phase: O(live graph)
- Sweep phase: O(heap size)
- Avoid pathological GC frequency via adaptive thresholds

## 14. Safety and Diagnostics

Optional debug build:

- Poison freed memory
- Detect double-free
- Verify mark-bit reset correctness
- GC logging

On allocation failure:

1. Trigger `Gc.collect()`
2. Retry allocation
3. If still failing, raise a runtime out-of-memory error

## 15. Non-Goals (v1)

- Incremental or concurrent GC
- Moving / compacting GC
- High-level finalizers
- Conservative stack scanning

---

## Appendix: Minimal Test Scenarios

1. Simple object cycle collection
2. Closure capturing environment values
3. Prototype chain cycles
4. Stress mode (GC on every allocation)
5. I/O handle lifetime correctness (no implicit close)

