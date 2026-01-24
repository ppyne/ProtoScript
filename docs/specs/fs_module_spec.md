# ProtoScript Fs Module Specification

This document defines the **Fs** module of ProtoScript. The module provides synchronous filesystem operations. All functions are explicit, side‑effectful, and return simple scalar values or objects. No exceptions are raised by this module; error conditions are reported via return values.

---

## Design Principles

- All operations are synchronous.
- The Fs module is **POSIX-only**.
- No implicit directory creation or deletion.
- No globbing or wildcard expansion.
- Paths are handled as raw strings; no normalization is performed implicitly.
- Errors are reported by `false`, `undefined`, or empty results, never by exceptions.
- Symbolic links are treated according to the underlying OS semantics.

---

## Platform & Compilation Control

### POSIX scope

The Fs module is designed exclusively for POSIX-compatible systems (Linux, BSD, macOS). No Windows compatibility layer is provided or implied.

All behaviors described in this specification assume standard POSIX filesystem semantics.

### Compile-time enable / disable

The Fs module can be enabled or disabled at compile time using the following configuration macro:

```c
#define PS_ENABLE_MODULE_FS 1
```

- `PS_ENABLE_MODULE_FS = 1` (default): the Fs module is available.
- `PS_ENABLE_MODULE_FS = 0`: the Fs module is completely disabled.

When disabled:
- The `Fs` object is not defined in the global namespace.
- Any attempt to access `Fs` results in a runtime reference error.
- No filesystem-related code is linked or compiled.

---

## Error Semantics & Edge Cases

This section defines how the Fs module behaves in error conditions and uncommon filesystem situations. These rules are normative and apply to all Fs functions unless stated otherwise.

### Non‑existent paths

- Any function receiving a path that does not exist returns:
  - `false` for boolean-returning functions.
  - `undefined` for value-returning functions (`Fs.size`).
  - An empty array for `Fs.ls`.
- No directory or file is created implicitly.

### Permissions

- If the current process lacks sufficient permissions:
  - The function returns `false` (or `undefined` for `Fs.size`).
  - No partial operation is performed.
- `Fs.isReadable`, `Fs.isWritable`, and `Fs.isExecutable` reflect **effective permissions** of the current process, not filesystem metadata alone.
- `Fs.chmod` fails and returns `false` if the process lacks permission to change mode bits.

### Symbolic links

- Symbolic links are resolved implicitly by the underlying operating system.
- `Fs.exists` returns `true` for valid symlinks, even if the target does not exist.
- `Fs.isFile` / `Fs.isDir` return:
  - `true` if the resolved target is a file or directory.
  - `false` if the symlink is broken.
- `Fs.size` returns `undefined` for broken symlinks.
- `Fs.isSymlink` returns `true` for any symlink, including broken ones.
- No function provides explicit symlink manipulation in this version.

### Directories vs files

- Functions expecting files (`Fs.cp`, `Fs.rm`, `Fs.size`) return `false` or `undefined` when given a directory.
- Functions expecting directories (`Fs.ls`, `Fs.rmdir`) return `false` or an empty array when given a file.

### Overwriting behavior

- `Fs.cp` overwrites the destination file if it exists.
- `Fs.mv` overwrites the destination if permitted by the OS.
- No automatic backup or confirmation is performed.

### Atomicity guarantees

- Each Fs operation is atomic at the function level.
- Partial copies or moves are not visible to the program if the function returns `false`.

---

## Fs.chmod(path, mode) : bool

### Description
Changes file permissions for `path` using a numeric mode.

### Parameters
- **path**: string — filesystem path.
- **mode**: number — numeric permission mode (e.g. `0644`).

### Return Value
- `true` on success.
- `false` on failure.

### Example
```js
Fs.chmod("my_file.txt", 0644);
```

---

## Fs.cp(source, destination) : bool

### Description
Copies a file from `source` to `destination`. If `destination` exists, it is overwritten.

### Parameters
- **source**: string — path to the source file.
- **destination**: string — path to the destination file.

### Return Value
- `true` on success.
- `false` on failure.

### Example
```js
Fs.cp("a.txt", "b.txt");
```

---

## Fs.exists(path) : bool

### Description
Checks whether a filesystem path exists.

### Parameters
- **path**: string — filesystem path.

### Return Value
- `true` if the path exists.
- `false` otherwise.

### Example
```js
Fs.exists("existing.txt");
```

---

## Fs.size(path) : int | undefined

### Description
Returns the size of a file in bytes.

### Parameters
- **path**: string — filesystem path.

### Return Value
- File size in bytes.
- `undefined` on error or if the path is not a regular file.

### Example
```js
Fs.size("somefile.txt");
```

---

## Fs.isDir(path) : bool

### Description
Checks whether `path` refers to a directory.

### Parameters
- **path**: string — filesystem path.

### Return Value
- `true` if directory.
- `false` otherwise.

### Example
```js
Fs.isDir("..");
```

---

## Fs.isFile(path) : bool

### Description
Checks whether `path` refers to a regular file.

### Parameters
- **path**: string — filesystem path.

### Return Value
- `true` if regular file.
- `false` otherwise.

### Example
```js
Fs.isFile("README.md");
```

---

## Fs.isSymlink(path) : bool

### Description
Checks whether `path` is a symbolic link.

### Parameters
- **path**: string — filesystem path.

### Return Value
- `true` if symbolic link.
- `false` otherwise.

### Example
```js
Fs.isSymlink("link.txt");
```

---

## Fs.isExecutable(path) : bool

### Description
Checks whether the file is executable by the current process.

### Parameters
- **path**: string — filesystem path.

### Return Value
- `true` if executable.
- `false` otherwise.

---

## Fs.isReadable(path) : bool

### Description
Checks whether the file is readable by the current process.

### Parameters
- **path**: string — filesystem path.

### Return Value
- `true` if readable.
- `false` otherwise.

---

## Fs.isWritable(path) : bool

### Description
Checks whether the file is writable by the current process.

### Parameters
- **path**: string — filesystem path.

### Return Value
- `true` if writable.
- `false` otherwise.

---

## Fs.ls(path, all = false, limit = 0) : array

### Description
Lists directory entries. Entries are returned as strings. `.` and `..` are excluded unless `all` is `true`.
If `limit` is provided and greater than 0, at most that many entries are returned.

### Parameters
- **path**: string — directory path.
- **all**: boolean — include dotfiles and `.` / `..`.
- **limit**: number — maximum entries to return (0 = unlimited).

### Return Value
- Array of entry names.
- Empty array on error.

### Example
```js
var path = "./";
var files = Fs.ls(path);
for (file of files) {
    if (Fs.isDir(path + file)) Io.print("directory: " + file + Io.EOL);
    else Io.print("file: " + file + Io.EOL);
}
```

### Recursive example (do not follow symlinks)
```js
function walkTree(path, prefix = "") {
    if (path.lastIndexOf('/') !== path.length - 1) path += "/";
    var files = Fs.ls(path, true, 200);
    for (file of files) {
        var full = path + file;
        if (Fs.isSymlink(full)) {
            Io.print(prefix + "├─ " + file + "@" + Io.EOL);
        } else if (Fs.isDir(full)) {
            Io.print(prefix + "├─ " + file + "/" + Io.EOL);
            walkTree(full, prefix + "│  ");
        } else {
            Io.print(prefix + "├─ " + file + Io.EOL);
        }
    }
}
walkTree("./");
```

---

## Fs.mkdir(path) : bool

### Description
Creates a directory.

### Parameters
- **path**: string — directory path.

### Return Value
- `true` on success.
- `false` on failure.

### Example
```js
Fs.mkdir("tmp");
```

---

## Fs.mv(source, destination) : bool

### Description
Renames or moves a file. If `destination` contains no `/`, the file is renamed in the source directory.

### Parameters
- **source**: string — source path.
- **destination**: string — new path or name.

### Return Value
- `true` on success.
- `false` on failure.

### Example
```js
Fs.mv("a.txt", "b.txt");
```

---

## Fs.pathInfo(path) : object

### Description
Returns path components.

### Parameters
- **path**: string — path to analyze.

### Return Value
Object with properties:
- `dirname`
- `basename`
- `filename`
- `extension`

### Example
```js
var infos = Fs.pathInfo("/a/b.txt");
for (info in infos) Io.print(info + ': ' + infos[info] + Io.EOL);
```

---

## Fs.pwd() : string

### Description
Returns the current working directory.

### Parameters
None.

### Return Value
- Current working directory.

### Example
```js
Fs.pwd();
```

---

## Fs.cd(path) : bool

### Description
Changes the current working directory.

### Parameters
- **path**: string — directory path.

### Return Value
- `true` on success.
- `false` on failure.

### Example
```js
Fs.cd("tmp");
```

---

## Fs.rmdir(path) : bool

### Description
Removes an empty directory.

### Parameters
- **path**: string — directory path.

### Return Value
- `true` on success.
- `false` on failure.

### Example
```js
var path = "example";
if (Fs.isDir(path) && Fs.ls(path).length == 0) {
    Fs.rmdir(path);
}
```

---

## Fs.rm(path) : bool

### Description
Removes a file.

### Parameters
- **path**: string — filesystem path.

### Return Value
- `true` on success.
- `false` on failure.

### Example
```js
if (Fs.exists(filename)) Fs.rm(filename);
```


---

## Fs Test Matrix

(see matrix above)

---

## Documentation Requirements

This specification imposes mandatory documentation requirements for the Fs module.

### Scope

- All existing ProtoScript documentation must be reviewed and updated to remain consistent with this specification.
- No outdated, partial, or implicit description of filesystem behavior is acceptable.

### User-facing documentation

Documentation intended for users **must**:

- Describe **all available Fs functions**.
- Clearly state that Fs is **POSIX-only**.
- Explicitly mention the compile-time flag `PS_ENABLE_MODULE_FS` and its effects.
- Document all return values and failure cases.
- Include **all examples provided in this specification**.

### Examples and pedagogy

- All examples in this specification must be reused verbatim or adapted faithfully.
- Additional practical examples must be added for:
  - directory traversal,
  - permission handling,
  - error-safe filesystem manipulation,
  - typical scripting use cases.
- Examples must be runnable, minimal, and free of undefined behavior.

### Examples validation & repository layout

- All examples **must** be placed in the `examples/` directory (or subdirectories) of the ProtoScript repository.
- Each example file must be:
  - self-contained,
  - clearly named,
  - directly executable in a standard ProtoScript environment.
- Every example must be **executed at least once** before release to verify:
  - absence of runtime errors,
  - correct and complete behavior.
- Examples that are untested, broken, or outdated are not acceptable.

Failure to comply with these requirements invalidates the documentation.

---

## Fs Test Plan

This section defines the execution plan for validating the Fs module against the test matrix. It describes test structure, ordering, fixtures, and expected guarantees. This plan is normative for the reference implementation.

### 1. Scope

- Applies only when `PS_ENABLE_MODULE_FS = 1`.
- All tests are POSIX-only.
- Tests must not rely on external system state.
- No test may modify files outside its own sandbox.

---

### 2. Test Environment

Each test run must create a dedicated temporary root directory:

- Location: system temporary directory (e.g. `/tmp`).
- Name: unique per test run (PID + random suffix).
- All filesystem operations must be relative to this root.

At the end of the test run:
- The entire tree is recursively removed by the test harness (not by Fs).

---

### 3. Directory Layout Fixture

Initial layout created before functional tests:

```
root/
├─ file.txt           (0644, content: "abc")
├─ exec.sh            (0755)
├─ empty_dir/
├─ non_empty_dir/
│  └─ nested.txt
├─ unreadable.txt     (0000)
├─ symlink_file  -> file.txt
├─ symlink_dir   -> empty_dir/
├─ broken_symlink -> missing.txt
```

All permissions must be set explicitly during fixture creation.

---

### 4. Test Ordering

Tests must be executed in the following order:

1. Compile-time tests
2. Existence and type checks
3. Permission checks
4. Read-only operations
5. Mutating operations
6. Cleanup verification

Each step assumes the previous steps succeeded.

---

### 5. Compile-time Tests

Executed in two builds:

#### Build A — Fs enabled

- `PS_ENABLE_MODULE_FS = 1`
- Verify `Fs` exists in the global namespace.

#### Build B — Fs disabled

- `PS_ENABLE_MODULE_FS = 0`
- Any reference to `Fs` must raise a runtime reference error.
- No Fs symbols must be present in the binary.

---

### 6. Functional Tests

Each Fs function must have:

- At least one success case.
- At least two failure cases.
- Coverage for:
  - non-existent paths
  - permission denial
  - incorrect path type (file vs dir)

Tests must assert:
- Return value only (never side effects on failure).
- No partial filesystem modification.

---

### 7. Atomicity Tests

For `Fs.cp` and `Fs.mv`:

- Simulate failure conditions (permission denied, invalid destination).
- Verify destination does not exist or is unchanged after failure.

---

### 8. Symlink Tests

- Validate resolution behavior for all functions.
- Broken symlinks must never be treated as files or directories.
- `Fs.exists` must return `true` for broken symlinks.

---

### 9. Idempotency & Safety

- Repeating a failing operation must not change the filesystem.
- Successful operations must leave the filesystem in a consistent state.

---

### 10. Cleanup Verification

At the end of the test run:

- All files created by Fs operations must be removable.
- No permission or ownership leaks are allowed.

Failure to clean up is a test failure.
