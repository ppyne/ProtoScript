# ProtoScript Performance-Oriented Execution Specification

## 1. Scope and Goal

This document defines a **strict, performance-first execution model** for ProtoScript.

Primary objective:

> Achieve performance comparable to QuickJS **as fast as possible**, with **minimal engineering effort**, by enforcing architectural constraints and eliminating low-ROI optimizations.

This specification is **binding** for any work delegated to Codex or contributors.

---

## 2. Current Situation (Problem Statement)

ProtoScript currently uses a **hybrid execution model**:

- AST-based evaluation
- Expression-level bytecode with dynamic fallback
- Statement-level bytecode per function

While functionally correct, this model introduces:

- Multiple execution paths per function
- Conditional dispatch and fallback logic in hot paths
- Fragmented bytecode with poor instruction locality

Result:

- High per-instruction overhead
- Optimizations with poor or invisible impact
- Difficulty correlating changes with performance gains

---

## 3. Non-Negotiable Architectural Principles

### 3.1 Single Execution Path per Function

After compilation, **each function must execute using exactly one engine**.

Rules:

- AST evaluation is **front-end only**
- After `stmt_bc_compile`, AST execution is forbidden
- No dynamic fallback to AST or alternative evaluators

Rationale:

> Removing branches and execution ambiguity yields more performance than micro-optimizations.

---

### 3.2 Function-Wide Linear Bytecode

Bytecode must be:

- Linear
- Contiguous
- Function-wide

Rules:

- No per-node or per-expression bytecode caches
- Expressions are compiled inline into the function bytecode
- One bytecode stream per function

Rationale:

> Fragmented bytecode destroys cache locality and increases dispatch cost.

---

### 3.3 Bytecode Is Mandatory for Execution

Rules:

- If a construct cannot be compiled to bytecode, it must be rejected at compile time
- Runtime fallback mechanisms are forbidden in hot paths

Rationale:

> A slow fallback executed often is worse than a missing feature.

---

## 4. Interpreter Requirements

### 4.1 Threaded Interpreter (Release Builds)

- The statement bytecode interpreter **must use a threaded dispatch** (computed goto)
- A switch-based interpreter may exist only for debug builds

Rationale:

> Dispatch cost dominates execution time in tight loops.

---

### 4.2 Stable Hot Path

The following are forbidden inside the main interpreter loop:

- Dynamic opcode capability checks
- Feature flags
- Debug conditionals
- Allocation

---

## 5. Measurement and Validation Rules

### 5.1 Mandatory Microbenchmark

A minimal arithmetic loop benchmark must exist:

```js
var x = 0;
for (var i = 0; i < 10000000; i = i + 1) {
  x = x + i;
}
ProtoScript.exit(x);
```

This benchmark is the **primary performance signal**.

---

### 5.2 Acceptance Criteria for Changes

Any change must satisfy:

- ≥ +5% improvement on at least one benchmark, OR
- No measurable regression on all benchmarks

Automatic rejection if:

- ≥ -2% regression on a core benchmark
- No before/after measurements provided

---

### 5.3 Forbidden Work

The following are explicitly forbidden until the core goals are met:

- Refactoring for readability
- Abstraction layers
- GC redesign
- Polymorphic inline caches
- Speculative optimizations

---

## 6. Optimization Priority Order (Strict)

1. Eliminate AST execution after compilation
2. Merge expression and statement bytecode into one stream
3. Threaded interpreter for statement bytecode
4. Fast-path arithmetic opcodes (int / double)
5. Monomorphic inline cache for property access

Any deviation requires explicit justification and benchmarks.

---

## 7.  Operating Rules

We must:

- Minimize diff size
- Touch the fewest files possible
- Optimize only confirmed hot paths
- Reject changes without measurable gains

We must never:

- Introduce new execution paths
- Add dynamic checks to the interpreter loop
- Optimize cold code

---

## 8. Definition of Success

ProtoScript is considered successful when:

- The arithmetic loop benchmark is within the same order of magnitude as QuickJS
- The interpreter loop contains no dynamic branches unrelated to opcode dispatch
- Performance improvements correlate directly with architectural simplification

---

End of specification.

