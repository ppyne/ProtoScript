# ProtoScript Performance Roadmap (Codex-Guided)

## Purpose

This roadmap defines a **strict, staged plan** to bring ProtoScript performance close to QuickJS **with minimal wasted effort**.

It is designed to:

- Prevent low-ROI optimizations
- Avoid performance regressions
- Give Codex a clear, enforceable direction

Each phase must be completed **in order**. Skipping phases is forbidden.

---

## Progress Log (Record of Accepted Phases)

Use the strict protocol unless stated otherwise: **30 runs, discard first (warmup), report median + min/max**.

- **Phase 0 accepted**: baseline established.

- **Phase 1 accepted**: `bench/arith_loop.js` wall-time median **0.86 s**, min/max **0.86–0.87 s**.

- **Phase 2 accepted**: `bench/arith_loop.js` wall-time median **0.74 s**, min/max **0.73–0.75 s** (**\~14% faster** vs Phase 1 baseline).

- **Phase 3 accepted**: `bench/arith_loop.js` wall-time median **0.63 s**, min/max **0.62–0.66 s** (**\~15% faster** vs Phase 2 baseline).

---

---

## Phase 0 — Lock the Performance Signal (Mandatory)

### Objective

Establish a reliable, low-noise performance baseline.

### Actions

- Add `bench/arith_loop.js` (pure arithmetic loop, no I/O)
- Define a single benchmark command (Release build only)
- Run benchmark multiple times and record median
- Use a strict protocol: **30 runs, discard first (warmup), report median + min/max**

### Exit Criteria

- Stable timing within ±2–3% variance across the post-warmup runs
- Baseline numbers committed or recorded

### Notes

Until this phase is complete:

> **No optimization work is allowed.**

---

## Phase 1 — Eliminate AST Execution After Compilation

### Objective

Remove execution-path ambiguity and branch overhead.

### Actions

- Treat AST as **front-end only**
- After `stmt_bc_compile`, forbid AST evaluation
- Replace runtime fallbacks (AST) with compile-time errors or runtime throws
- Ensure benchmark-only instrumentation is strictly gated (env var) and off by default

### Exit Criteria

- All functions execute via `stmt_bc_execute`
- No AST fallback reachable in hot paths
- Wall-time strict protocol shows no ≥ -2% regression on `bench/arith_loop.js`

---

## Phase 2 — Unify Bytecode into a Single Function Stream

### Objective

Improve instruction locality and reduce dispatch overhead.

### Actions

- Stop using per-expression bytecode caches
- Generate one **linear bytecode stream per function**
- Inline expressions directly into statement bytecode

### Constraints

- No new IR layer
- No multi-pass compilation
- Minimal code changes

### Exit Criteria

- One bytecode array per function
- No expression-level bytecode executed at runtime
- **Strict-protocol benchmark shows ≥ +5% improvement on ****`bench/arith_loop.js`**

### Status

- **Completed**: single linear statement bytecode stream implemented
- **Measured result**: median improved from 0.86 s → 0.74 s (\~14% gain)
- No regressions observed under strict protocol

---

## Phase 3 — Introduce a Threaded Interpreter (Computed Goto)

### Objective

Reduce opcode dispatch cost in tight loops.

### Actions

- Implement a threaded interpreter for statement bytecode
- Keep switch-based interpreter for debug builds only (or compile-time fallback)
- Ensure zero feature checks inside the main dispatch loop

### Expected Impact

- +20% to +70% speedup depending on baseline

### Exit Criteria

- Threaded interpreter enabled in Release builds
- Clear performance gain on `arith_loop.js`

### Status

- **Completed**: computed-goto threaded dispatch implemented for statement bytecode execution
- **Compile-time fallback**: `-DPS_DISABLE_THREADED_DISPATCH` forces switch dispatch
- **Measured result**: median improved from 0.74 s → 0.63 s (\~15% gain), min/max 0.62–0.66 s
- No regressions observed under strict protocol

---

## Phase 4 — Add Arithmetic Fast Paths

### Objective

Eliminate dynamic type overhead in hot arithmetic loops.

### Actions

- Introduce specialized opcodes (`ADD_INT`, `ADD_DOUBLE`, etc.)
- Keep a single slow fallback path outside hot loops
- Ensure no allocations in arithmetic paths

### Exit Criteria

- Significant speedup on arithmetic benchmark
- No regressions on other benchmarks

---

## Phase 5 — Monomorphic Inline Cache for Property Access (Optional)

### Objective

Close the remaining gap with QuickJS on object-heavy code.

### Actions

- Implement monomorphic inline cache (shape + offset)
- Simple invalidation strategy
- No polymorphic or megamorphic support at this stage

### Preconditions

- Phases 0–4 completed and validated

### Exit Criteria

- Measurable gain on object-access microbench

---

## Rejection Rules (Global)

A change must be rejected if:

- No before/after benchmark is provided
- A core benchmark regresses by ≥ -2% **after a strict protocol run**
- The diff introduces new execution paths or dynamic checks

Notes:

- If results are noisy or show outliers, first re-run with the strict protocol and (optionally) split timings before accepting a regression claim.

---

## Codex Usage Rules

When delegating work to Codex:

- Assign **one phase at a time**
- Require benchmark output with every change
- Prefer small, reversible commits

Codex must not:

- Perform refactors for clarity
- Add abstraction layers
- Optimize GC or memory before Phase 4
- Touch cold or rarely executed code

---

## Temporary Benchmark Instrumentation Rules

Sometimes minimal, benchmark-only instrumentation is required to isolate costs (e.g., compile vs execute time).

Allowed (must be guarded by an env var, e.g. `PS_BENCH_SPLIT_TIMING=1`):

- Printing `compileMs` / `execMs` (or equivalent) for a benchmark
- Small control-flow adjustments needed to flush timing output
- Adding missing bytecode support **only** to avoid falling back to AST for the benchmark (this is a correctness fix under Phase 1)

Forbidden:

- Instrumentation enabled by default
- Changes that add checks to the interpreter hot loop when instrumentation is off
- Benchmark-only semantics leaking into normal execution

---

## Definition of Completion

This roadmap is considered complete when:

- The arithmetic loop benchmark is within the same order of magnitude as QuickJS
- Performance improvements are consistent, explainable, and repeatable
- No optimization work depends on fallback mechanisms or speculative behavior

---

End of roadmap.

