# ProtoScript — Graphics & I/O Roadmap (for Codex)

This document decomposes the implementation work into **explicit, ordered tasks** suitable for execution by Codex or a similar code-generation agent.

The roadmap follows the validated architectural order and avoids circular dependencies.

---

## Phase 0 — Preconditions

**Assumed existing components:**

- Core ProtoScript runtime
- Object model and GC
- Existing `Io` text API (`read`, `write`, `print`)

No graphics, no binary memory, no events yet.

---

## Phase 1 — Buffer (foundation)

### Goal

Introduce a minimal binary memory abstraction usable by all lower-level subsystems.

### Tasks

1. Define internal native structure for `Buffer`
   - contiguous memory allocation
   - fixed size
   - byte-addressable (uint8)

2. Implement `Buffer.alloc(size)`
   - argument validation
   - zero-initialization
   - GC-managed lifetime

3. Implement `Buffer.size(buffer)`

4. Implement indexed access semantics
   - `buf[i]` read → number 0–255
   - `buf[i] = value` write → clamp to 0–255
   - bounds checking (throw on error)

5. (Optional) Implement `Buffer.slice(buffer, offset, length)`
   - copy semantics (no views)

### Deliverables

- `Buffer` module available in ProtoScript
- Unit tests for allocation and indexing

---

## Phase 2 — Io Binary Extension

### Goal

Enable explicit binary persistence using `Buffer`.

### Tasks

1. Extend native `Io` module with binary helpers

2. Implement `Io.readBinary(path)`
   - open file in binary mode
   - determine file size
   - allocate Buffer
   - read full contents
   - close file

3. Implement `Io.writeBinary(path, buffer)`
   - validate Buffer argument
   - open / create file in binary mode
   - truncate
   - write full buffer
   - close file

4. Enforce strict separation from text APIs
   - reject Buffer in `Io.write`
   - reject binary in `Io.read`

### Deliverables

- Binary-safe file I/O
- No changes to existing text semantics

---

## Phase 3 — Event System (native core)

### Goal

Provide a deterministic, pull-based event queue independent of rendering.

### Tasks

1. Define native event queue structure (FIFO)
   - bounded size
   - drop or error policy on overflow

2. Implement `Event.next()`
   - return next event object
   - return `null` if empty

3. (Optional) Implement `Event.clear()`

4. Define native event object representation
   - flat objects
   - string `type` field

5. No callbacks, no listeners, no dispatch logic

### Deliverables

- Stable event queue API
- Test events injected manually (before SDL integration)

---

## Phase 4 — Display (window + software rendering)

### Goal

Expose a portable window and a software framebuffer backed by `Buffer`.

### Tasks

1. Integrate SDL2 at the native layer
   - window creation
   - software surface or texture

2. Implement `Display.open(width, height, title)`
   - initialize SDL
   - allocate framebuffer Buffer (RGBA 8-bit)

3. Implement `Display.close()`
   - destroy window
   - release native resources

4. Implement drawing primitives
   - `clear`
   - `pixel`
   - `line`
   - `rect`
   - `fillRect`

5. Implement `Display.framebuffer()`
   - return live Buffer
   - handle resize reallocation

6. Implement `Display.present()`
   - blit Buffer to window
   - vsync if available

7. Translate SDL events into core Event queue
   - keyboard
   - mouse
   - window close / resize

### Deliverables

- Cross-platform window
- Software-rendered framebuffer
- Working input events

---

## Phase 5 — Validation Scenarios

### Minimal tests

- Open window → clear → present
- Draw pixels via framebuffer access
- Handle quit event

### Reference examples

- Pixel test pattern
- Mouse-follow square
- Simple image loaded via `Io.readBinary`

---

## Explicit Non-Goals (All Phases)

- GPU rendering
- Widgets or UI toolkit
- DOM-like event propagation
- Streaming I/O
- Bitwise operators
- Dedicated `Byte` type

---

## Philosophy Reminder

Each phase must:

- compile independently
- be testable in isolation
- add **one concept only**

ProtoScript grows by **stacking primitives**, not frameworks.

