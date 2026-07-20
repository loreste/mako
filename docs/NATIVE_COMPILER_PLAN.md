# Native compiler plan (replace Mako → C → clang)

## Goal

Replace the C backend with mako's own compiler that emits machine code directly.
Priorities, in order: **runtime speed** (faster than C/Rust per workload),
**fast compiles**, and **zero install-time dependencies** for end users.

## Architecture (decided)

Everything is statically linked into the `mako` binary. Users install one file and
depend on no external toolchain.

```
.mko → Rust frontend (reused) → native IR (ownership-explicit)
        ├── release → LLVM (static)      → object   ← runtime speed ≥ C/Rust
        └── debug   → Cranelift (static) → object   ← fastest compiles
                                   → lld (static)    → executable
```

- **No C is generated or parsed**, ever. This removes both the C-compile latency and
  the clang/system-lib install dependency.
- **Release backend = statically-linked LLVM**, emitting LLVM IR directly. Same
  optimizer C and Rust use, so runtime parity is by construction. We *beat* them
  per-workload by feeding LLVM better IR than clang/rustc can: aliasing-by-default,
  no pointer UB, ownership-driven bounds-check elision, guaranteed devirtualization,
  and PGO.
- **Debug backend = statically-linked Cranelift** (the existing `src/native_codegen.rs`).
  Optimizes for compile speed; beats clang debug builds.
- **Linker = bundled `lld`** (ships with LLVM). No external `ld` needed.
- **Runtime C library** is precompiled once (when mako is built) into a static
  archive shipped inside mako and linked by `lld`. No per-program C compilation.
  Hot pieces migrate to Mako over time.

### Why not Cranelift alone

Cranelift trails LLVM at runtime; a Cranelift-only Mako would be *slower* than C and
Rust, not faster. It stays as the debug backend only.

### Honest performance bar

"Faster than C and Rust" is evaluated **per published workload**, not asserted
universally — consistent with the gate in `docs/SELF_HOSTING.md`. Each result records
flags, hardware, samples, variance, RSS, and binary size.

## Current state (baseline)

- `src/native_codegen.rs` (~928 lines): AST → Cranelift → host object, linked via `cc`.
  Supports scalars (int/bool/float), arithmetic/compare/logic, if/while/break/continue,
  direct calls, and `print` of int/bool/**string literals**. Compiles `hello`, `integers`.
- Gap to the C backend (`src/codegen/mod.rs`, ~35k lines): string *values*, ownership/
  drop insertion, structs/enums/slices/maps/tuples, `for`/`match`/`defer`, concurrency,
  runtime interop, cross/wasm/static/sanitizer/overflow-checked builds.

## Runtime string ABI (must stay differential-compatible)

`MakoString { char* data; size_t len }`, heap-allocated + NUL-terminated, freed with
`free(data)`. Empty and string *literals* are non-owned **views** (`mako_str_view`,
never freed); only heap strings (concat, formatting, etc.) are owned and dropped.
`print` → `mako_print_str` (newline-terminated).

## Increment roadmap

Each increment adds a positive differential fixture (native vs C: stdout/stderr/exit)
and, where relevant, a memory-safety fixture. Ship order is chosen so real programs
compile as early as possible.

1. **String values (views)** — strings as first-class values (locals, params, returns,
   `print`), literals as non-owned views. No heap yet. *[done — `(data,len)` register
   pair; differential fixture `examples/native/native_strings.mko`]*
2. **Heap strings + ownership** — `+` concat → heap, linear move/drop insertion, drop
   on scope exit and on not-taken CFG paths. This is the core ownership model.
   *[done — see "Ownership model" below; verified 0 leaks / 0 double-frees under
   Guard Malloc + `leaks` in the gate (`[4b/5]`)]*
3. **Aggregates** — structs, tuples, slices (`[]T`), maps; `make`, indexing, slicing,
   field access, literals; their ownership/drops.
   - *3a [done]* — `[]int` slices: array literals, `make([]int, n[, cap])` (calloc),
     bounds-checked indexing, `len`, iteration, reassignment (drop-on-reassign),
     move between locals, indexing/`len` of owned temporaries, and freeing discarded
     owned temporaries. `(data, len, cap)` triple matching `MakoIntArray`; owned when
     `cap > 0`. Fixture `examples/native/native_slices.mko`; 0 leaks under the gate.
   - *3b [done]* — slice **parameters/returns** (3-slot `data,len,cap` ABI; returns
     always owned, borrowed/view returns cloned), **index assignment** (`a[i] = v`,
     bounds-checked), and **`append(xs, v)`** (self-consuming: in-place growth when
     `len < cap`, else 2x realloc + free-of-old; borrowed sources copied, never
     mutated). Enables real slice functions (`sum([]int) -> int`,
     `squares/tens(int) -> []int`, append-in-a-loop). Still deferred: slicing
     (`a[i:j]`) and `[]string`/`[]float`/`[]bool`/nested slices.
   - *3c [done]* — **structs with scalar fields** (value semantics): struct
     registry, literals, field read/write, functional update (`..base`), struct
     parameters/returns (flattened one ABI slot per field), mixed scalar field
     kinds, and value-copy semantics. No heap, so no drops. Fixture
     `examples/native/native_structs.mko`. Deferred: struct fields of
     string/slice/struct type (needs struct drops + nested layout), and methods.
     NOTE: native fixtures live in `examples/native/`, not `examples/testing/` —
     `mako test` compiles the latter's non-`_test.mko` files as shared sources.
4. **Control flow** — `for`, c-style `for`, `match`, `defer`, labeled loops.
   - *4-for [done]* — `for` loops: counted `for i in n` / `for i in range n`
     (0..n), and `[]int` iteration `for i, v in range xs` (index + value) or
     `for i in range xs` (index only, Go semantics). `continue` targets an
     increment latch; `break`/nesting supported. Owned-temporary iteration is
     rejected (bind to a local first). Fixture `examples/native/native_for.mko`.
     Deferred: c-style `for init; cond; post`, `defer`, labeled loops.
   - *4-match [done]* — scalar `match` (int/bool scrutinee): literal arms,
     or-patterns (`1 | 2 | 3`), wildcard, exhaustive bool, and identifier
     catch-all that binds the scrutinee. Lowered as a linear decision chain with
     a merge block parameter; the last arm is the fallthrough (frontend guarantees
     exhaustiveness). Fixture `examples/native/native_match.mko`. Deferred: enum/
     variant/tuple/struct patterns, guards, and non-scalar scrutinees.
5. **Runtime interop** — call the precompiled runtime archive (net/db/tls/fmt/…) via
   the native ABI instead of libc-only.
6. **Concurrency** — `crew`/`kick`/`fan`/channels/`select`.
7. **LLVM release backend** — same IR → LLVM IR, statically linked; benchmark vs C/Rust.
8. **Bundled lld + runtime archive** — drop the external `cc`/`ld` dependency.
9. **Build modes** — cross, wasm, static, sanitizers, overflow-checked arithmetic.

## Ownership model (increment 2, implemented)

Strings are `(data, len)` register pairs. Each string local also carries a static
ownership flag (`str_owned`): `true` = owns a heap buffer that must be dropped;
`false` = a non-owned view/borrow or a moved-out local. Rules:

- **Literals** → non-owned views (`owned=false`); never freed.
- **`+` concat** → fresh heap buffer (`malloc(la+lb+1)`, copy, NUL) → owned temporary.
  Nested concat frees its owned operands after copying, so `a + b + c` leaks nothing.
- **Parameters** are borrows; the callee never frees them.
- **Function string returns are always owned heap**: a returned owned local is
  moved out (flag cleared so it is not also dropped); a view/borrow is cloned.
- **`print(x)`** borrows; an owned temporary argument is freed after it is written.
- **Call arguments** are passed as borrows; an owned temporary argument
  (`f(a + b)`) is freed after the call returns.
- **`let`/assignment**: a bare identifier RHS naming a string local is a MOVE
  (buffer transfers, source flag clears); reassigning a local that already owns a
  buffer drops the old buffer first.
- **Scope exit** (every return and fallthrough) drops all still-owned string locals.
- **Control flow**: a branch/loop body that reaches the merge/back-edge must leave
  the owned-set unchanged, else it is rejected (`string ownership that changes
  inside a branch/loop`). Reassigning an already-owned local inside a branch is
  allowed — Cranelift merges the pointer variable and each path frees once.

Known conservative limits (safe rejects, to be lifted later): heap strings created
inside a continuing branch or loop body; interprocedural borrow inference (returns
always clone views rather than borrowing). String equality/comparison and formatting
are not yet lowered.

## Verification

`./scripts/native-compiler-test.sh` (Rust unit tests, self-host frontend gate,
ownership + instrumented memory-safety regressions, and native/C differential
execution). New backend features must extend the differential fixture set before
they count as done.
