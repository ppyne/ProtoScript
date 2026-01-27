# ProtoScript Phase‑5 & Bottleneck Discovery Roadmap

## Purpose

This roadmap is **additive** to the main ProtoScript Performance Roadmap.

Its goals are:
- Identify the **current dominant bottleneck** after Phases 0–4
- Validate whether control‑flow and property access now dominate execution cost
- Prepare **strict, low‑risk conditions** for Phase 5 (monomorphic inline cache)

This roadmap is intentionally narrow and must not introduce speculative or high‑complexity work.

---

## Global Rules (Inherited)

Unless explicitly overridden, all rules from the main roadmap apply:
- Strict protocol: **30 runs, discard first, median + min/max**
- Reject ≥ **-2% regression** on any core benchmark
- No runtime feature checks in hot paths
- No refactors without measured benefit

---

## Phase A — Add Control‑Flow Microbenchmark (Optional but Recommended)

### Objective

Determine whether **loop control + comparison + branch** is now the dominant cost after arithmetic and dispatch optimizations.

### Benchmark Definition

Create a new benchmark:

```js
// bench/branch_loop.js
var x = 0;
for (var i = 0; i < 10000000; i = i + 1) {
  if ((i & 1) === 0) {
    x = x + 1;
  }
}
ProtoScript.exit(x);
```

This benchmark stresses:
- loop condition (`i < N`)
- comparison
- branch predictability
- minimal arithmetic

### Protocol

- Release build
- 30 runs, discard first
- Record median + min/max

### Exit Criteria

- Stable distribution
- Result recorded as new reference signal

### Interpretation

- If runtime is close to `arith_loop.js`: control flow dominates
- If significantly slower: branch/compare overhead is now the primary target

---

## Phase B — Add Object Access Microbenchmark (Mandatory for Phase 5)

### Objective

Prove that property access is a remaining hot cost and provide a **verification signal** for inline caches.

### Benchmarks

#### 1) Simple repeated access (best-case)

```js
// bench/object_access.js
var o = { x: 0 };
var sum = 0;
for (var i = 0; i < 10000000; i = i + 1) {
  sum = sum + o.x;
}
ProtoScript.exit(sum);
```

Notes:
- This benchmark may be heavily optimized by a last-access micro-cache (e.g., cache_name/cache_prop).
- It is useful as a **no-regression** and “best-case” signal, but may be insufficiently discriminating for Phase 5 acceptance.

#### 2) Alternating keys (small object; may still be too easy)

```js
// bench/object_access_altkeys.js
var o = { x: 1, y: 2 };
var sum = 0;
for (var i = 0; i < 10000000; i = i + 1) {
  if ((i & 1) === 0) sum = sum + o.x;
  else              sum = sum + o.y;
}
ProtoScript.exit(sum);
```

Notes:
- Object shape is stable.
- Property access alternates keys.
- A last-access micro-cache will miss frequently.
- However, with only 2 properties, the generic slow path may be very cheap (short list scan, no buckets), so IC gains may be small.

#### 3) Alternating keys on a larger object (primary Phase-5 acceptance signal)

```js
// bench/object_access_altkeys_big.js
var o = {
  a0:0,a1:1,a2:2,a3:3,a4:4,a5:5,a6:6,a7:7,
  a8:8,a9:9,a10:10,a11:11,a12:12,a13:13,a14:14,a15:15,
  a16:16,a17:17,a18:18,a19:19,a20:20,a21:21,a22:22,a23:23,
  a24:24,a25:25,a26:26,a27:27,a28:28,a29:29,a30:30,a31:31
};

var sum = 0;
for (var i = 0; i < 10000000; i = i + 1) {
  if ((i & 1) === 0) sum = sum + o.a0;   // choose keys that are not both “recent”
  else              sum = sum + o.a17;
}
ProtoScript.exit(sum);
```

Notes:
- Object shape is stable.
- Many properties increase slow-path cost (longer scan and/or buckets enabled depending on runtime thresholds).
- Alternating keys defeats a last-access micro-cache.
- This benchmark is the **primary Phase-5 acceptance signal**.



### Protocol

- Release build
- 30 runs, discard first
- Record median + min/max

### Exit Criteria

- Stable baselines established for both benchmarks
- `object_access_altkeys.js` recorded as Phase-5 primary benchmark

---

## Phase 5 — Monomorphic Inline Cache for Property Access

### Objective

Reduce property access cost in hot loops by introducing a **strictly monomorphic inline cache**.

### Status (Result)

**Implemented and evaluated.**

Despite correct implementation (monomorphic, shape-based, no runtime checks) and clean correctness (`make test` passes), the inline cache yields only **~1–2% improvement** even on the most discriminating benchmark (`object_access_altkeys_big.js`).

This indicates that:
- The existing property lookup path (hash/list + last-access micro-cache) is already highly optimized.
- The remaining overhead is not dominated by repeated own-property resolution.
- A monomorphic IC does not materially reduce the dominant cost in the current object model.

Therefore, Phase 5 does **not** meet its performance acceptance criterion.

### Decision

**Phase 5 is concluded as non-beneficial in the current architecture.**

The implementation may be kept (it is safe and slightly beneficial), but Phase 5 is considered **closed without success** from a performance perspective.

---

## Phase 5 Validation Rules

### Mandatory Benchmarks

Phase 5 **must** be validated against:
- `bench/object_access_altkeys_big.js` (primary)
- `bench/object_access_altkeys.js` (secondary; small-object sanity)
- `bench/object_access.js` (no-regression / best-case)
- `bench/arith_loop.js` (regression check)
- `bench/branch_loop.js` (regression check, if present)
- `bench/object_access.js` (no-regression / best-case)
- `bench/arith_loop.js` (regression check)
- `bench/branch_loop.js` (regression check, if present)

### Acceptance Criteria

- ≥ **+10% improvement** on `bench/object_access_altkeys_big.js`
- No ≥ **-2% regression** on any other benchmark

If `object_access_altkeys_big` gain is < 10%, Phase 5 is considered **failed or incomplete**.

---

## Codex Task Template (Phase 5)

```
Implement Phase 5: monomorphic inline cache for property access.

Constraints:
- Monomorphic only (shape + offset)
- No polymorphic logic
- No runtime feature checks in the hot path
- Immediate invalidation on shape change
- Minimal diff

Benchmarks:
- bench/object_access_altkeys.js (must improve ≥10%)
- bench/object_access.js (no ≥-2% regression)
- bench/arith_loop.js (no ≥-2% regression)
- bench/branch_loop.js (if present, no ≥-2% regression)

Use strict protocol: 30 runs, discard first, median + min/max.
Reject if criteria are not met.
```

---

## Definition of Completion

This roadmap is complete when:
- Control‑flow cost is quantified via `branch_loop.js`
- Property access cost is reduced measurably via monomorphic inline cache
- Remaining bottlenecks are clearly identified and isolated

---

End of roadmap.

