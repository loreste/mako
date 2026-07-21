# Native compiler plan (Mako 0.4.1)

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

`./scripts/native-bench-gate.sh` enforces this bar on an output-equivalent
recursive/scalar/slice workload against the C backend, hand-written C, and
Rust. The first Apple arm64 run on 2026-07-20 failed at approximately 2.08× the
optimized C/Rust median. This result is expected evidence against using
Cranelift as the general release optimizer. Subsequent checked transforms include
recursive-addition elimination, wrapping Fibonacci fast doubling, SIMD slice
reduction, preallocated-append capacity proofs, non-escaping producer/reduction
fusion, and Mersenne-modulus reduction.

The gate now tests the combined workload plus Fibonacci and slice components
independently, and also gates source-to-binary latency, compiler RSS, runtime RSS,
and binary size. A seven-sample Apple arm64 run on 2026-07-20 measured the slice
component at 11.953 ms native versus 20.246 ms Mako C, 18.359 ms hand C, and
18.516 ms Rust (0.590–0.651×), with native runtime RSS at 0.135–0.151×. Native
compile latency was 58.973 ms versus 248.792 ms for the C backend (0.237×), with
compiler RSS at 0.451×. All three component runtime gates and the 1.01× binary
size gate pass. These remain explicit workload results rather than a universal
Cranelift parity claim; the optimizing LLVM release backend is still required
for broad workloads. Differential edge coverage lives in
`native_fibonacci.mko` and `examples/bench/native_slice.mko`.

The first LLVM release slice is measured independently on Apple arm64: LLVM
compilation was 20.1 ms median versus 251.9 ms through C, with 44 MB versus
101 MB compiler RSS. Fibonacci ran in 146.5 ms versus 148.9 ms Mako C, 147.8 ms
hand C, and 148.0 ms Rust; the LLVM binary was 33,488 bytes.

## Current state (baseline, v0.4.1)

- `src/native_codegen.rs`: Cranelift consumes the backend-neutral IR for the scalar
  increment and emits a host object. Aggregate/string programs still use the mature
  AST lowering while their Cranelift ownership ABI is migrated. Both native and LLVM
  release objects are linked by the bundled lld path on supported hosts.
- Gap to the C backend (`src/codegen/mod.rs`, ~35k lines): structs/enums/maps/tuples,
  `for`/`match`/`defer`, concurrency,
  runtime interop, cross/wasm/static/sanitizer/overflow-checked builds.
- Shared IR now covers scalar CFG, ownership-explicit strings, `[]int`, primitive
  slices, and `[]string` for the Cranelift native backend. Unsupported aggregates
  fail explicitly instead of silently falling back to C. The Linux x86_64 build
  is installable as a single `mako` binary; the release LLVM path remains the
  optimization target for broad workload parity.

## Runtime string ABI (must stay differential-compatible)

`MakoString { char* data; size_t len }`, heap-allocated + NUL-terminated, freed with
`free(data)`. Empty and string *literals* are non-owned **views** (`mako_str_view`,
never freed); only heap strings (concat, formatting, etc.) are owned and dropped.
`print` → `mako_print_str` (newline-terminated).

Cranelift's shared-IR path uses pointer wrappers in `runtime/native_runtime.c`
(`mako_native_string_*_ptr`); each wrapper owns a copied buffer and header, so
the explicit IR drop instruction remains backend-independent. LLVM continues to
use the value-layout ABI above.

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
   - *3b-leaks [done]* — **slice-view header leaks + owned-temp arg clone leak**
     (both backends, previously `native_slices` leaked 7 on Cranelift / 2 on
     LLVM). Two IR fixes: (1) slicing a *borrowed* base (`a[1:3]`) returns an
     **owned handle** — for `[]int` a non-owning view header that must be dropped
     (the runtime frees the header always but the shared data only when the
     header's `owned` flag is set, so no double-free), and for `[]string` an
     owned element copy; (2) an owned string/slice **temporary argument**
     (`sum(squares(6))`) is now handed to the callee as a borrow and dropped
     after the call instead of being cloned into a leaked copy. `native_slices`
     is now 0 leaks on both backends, GuardMalloc-clean, native≡LLVM.
   - *3b-fix [done]* — **`append` source consumption**: `let ys = append(xs, v)`
     consumes `xs` (the runtime grows in place or reallocs and frees the old
     buffer), so an owned source local must have its ownership cleared and must
     not be dropped again. The lowering now consults the source local's real
     ownership instead of the `Ident` read's `owned=false`. Fixes a `[]string`/
     `[]int` double-drop use-after-free that `native_string_slices` exposed under
     GuardMalloc (leak-clean and output-correct beforehand, but memory-unsafe).
   - *3b [done]* — slice **parameters/returns** (3-slot `data,len,cap` ABI; returns
     always owned, borrowed/view returns cloned), **index assignment** (`a[i] = v`,
     bounds-checked), and **`append(xs, v)`** (self-consuming: in-place growth when
     `len < cap`, else 2x realloc + free-of-old; borrowed sources copied, never
     mutated). Enables real slice functions (`sum([]int) -> int`,
     `squares/tens(int) -> []int`, append-in-a-loop). **Checked slicing is done**:
     `a[low:high]`, omitted bounds, and three-index syntax match the current C
     runtime's clamped non-owning-view behavior. Owned temporaries copy the selected
     range and free the original immediately; named bases remain sole owners.
     Differential and Guard Malloc coverage lives in `native_slices.mko`.
   - *3b-primitive [done]* — `[]float` and `[]bool` now share the typed slice
     lowering path: literals, `make`, checked indexing/assignment, `append`,
     slicing, `len`/`cap`, range iteration, parameters, and owned returns. Element
     strides are ABI-correct (8-byte float, 1-byte bool). Native float comparisons,
     int/float conversions, and fixed-ABI float printing are included. Fixtures
     `native_primitive_slices.mko` and `float_slice.mko` run under C/native
     differential coverage; the ownership fixture also runs under Guard Malloc.
     `[]string` is supported by both backends: Cranelift uses the pointer-header
     ABI (`mako_native_string_slice_*_ptr`); LLVM uses the value ABI
     (`mako_native_str_slice_*`, parallel to `[]int`). Nested slices of slices
     are still deferred.
   - *3c [done]* — **structs with scalar fields** (value semantics): struct
     registry, literals, field read/write, functional update (`..base`), struct
     parameters/returns (flattened one ABI slot per field), mixed scalar field
     kinds, and value-copy semantics. No heap, so no drops. Fixture
     `examples/native/native_structs.mko`. Deferred: struct fields of
     string/slice/struct type (needs struct drops + nested layout), and methods.
     NOTE: native fixtures live in `examples/native/`, not `examples/testing/` —
     `mako test` compiles the latter's non-`_test.mko` files as shared sources.
   - *3d [done]* — **scalar-field structs in the backend-neutral IR** (both
     Cranelift and LLVM consume them; this is what unblocks aggregates on the
     LLVM release backend). `src/native_ir.rs` gains `Type::Struct(id)`, a
     `StructLayout` registry on `Module`, and explicit `StructMake` /
     `StructField` / `StructFieldStore` / `StructClone` / `DropStruct`
     instructions. A struct value is an **owned heap block** of one 8-byte slot
     per scalar field, so it reuses the existing string/slice ownership
     machinery (drop on scope exit / not-taken CFG paths, move-out on return),
     with one difference: structs are **copy** types, so a binding, assignment,
     or by-value argument of a borrowed struct emits `StructClone` instead of a
     move. Covers named + positional literals, `..base` functional update, field
     read/write, struct params, and by-value struct returns. Cranelift lowers
     make/clone/drop through `mako_native_struct_{make,clone,drop}_ptr`
     (`runtime/native_runtime.c`) with inline typed field loads/stores; LLVM
     builds a concrete struct type per id and lowers make/field/clone via
     `malloc` + `getelementptr` + `llvm.memcpy` + `free`. `native_structs.mko`
     is now in the shared-IR-only differential list and passes on **both**
     backends (C-identical output, 0 leaks under `leaks`). Deferred: string /
     slice / nested-struct fields (need element drops + nested layout) and
     methods.
   - *3f [done]* — **`string` fields in structs** (owned aggregate fields). Struct
     layouts accept `string` fields alongside scalars, so `StructClone` and
     `DropStruct` become **recursive**: clone/drop each owned field (scalars are
     value copies), and `DropStruct` now carries the `struct_id` so each backend
     can look up field types. Construction moves an owned string temp in and
     clones a borrowed one; reading a field yields a borrow; reassigning an owned
     field drops the old value first; a functional update clones the owned fields
     it carries over from the base; and reading an owned field out of an owned
     temporary clones it before the temporary is freed. Fixture
     `native_owned_fields.mko` (construction, value-copy semantics, update,
     owned fields through params/returns) passes on Cranelift and LLVM
     (C-identical, 0 leaks, GuardMalloc-clean). Deferred: nested struct/tuple
     fields, tuples with owned elements, and enum owned payloads.
   - *3g [done]* — **`[]int` slice fields in structs**. Struct layouts accept
     `[]int` fields; the recursive `StructClone`/`DropStruct` now also clone/drop
     slice fields (Cranelift via the pointer-ABI `slice_clone`/`slice_drop`, LLVM
     via the value-ABI `mako_native_int_slice_{clone,drop}`). Construction moves
     an owned slice literal in / clones a borrow, indexing and `len` work through
     a field, by-value passing deep-clones the slice, and each owned slice field
     is dropped on scope exit. `native_slice_fields.mko` validated on both
     backends (C-identical, 0 leaks, GuardMalloc-clean).
   - *3e [done]* — **scalar tuples in the backend-neutral IR** (both backends).
     A tuple is lowered as an anonymous positional struct: each tuple *shape* is
     interned once into the same `StructLayout` list (fields named `"0"`, `"1"`,
     …), so tuples reuse `StructMake` / `StructField` / `StructClone` /
     `DropStruct` verbatim. Covers tuple literals `(a, b)`, tuple types in
     signatures/lets (`(int, int)`), by-value tuple parameters and returns, and
     multi-return destructuring `let a, b = f()` (`Stmt::LetMulti`, which
     extracts each scalar field into a fresh local and drops the tuple temp).
     Fixture `native_tuples.mko` passes on Cranelift and LLVM (C-identical, 0
     leaks under `leaks`, clean under GuardMalloc).
   - *3e-owned [done]* — **tuples with owned (`string`/`[]int`) elements + owned
     multi-return** (`let a, b = split()` where `split() -> (string, string)`).
     Tuples reuse the recursive `StructClone`/`DropStruct`, so the only new work
     is allowing owned element types and cloning each owned field on `LetMulti`
     extraction (the binding owns an independent copy; the tuple still drops its
     own). Fixture `native_owned_tuples.mko` passes on both backends
     (C-identical, 0 leaks, GuardMalloc-clean). Deferred: tuples with struct/enum
     elements.
4. **Control flow** — `for`, c-style `for`, `match`, `defer`, labeled loops.
   - *4-for [done]* — `for` loops in the **shared IR** (both backends): counted
     `for i in n` / `for i in range n` (0..n), and `[]int`/`[]string` iteration
     `for i, v in range xs` (index + value) or `for i in range xs` (index only).
     `continue` targets an increment latch; `break`/nesting supported via a loop
     stack (also wires unlabeled break/continue for `while`). Owned-temporary
     iteration is rejected (bind to a local first). Fixture
     `examples/native/native_for.mko` (C-identical on Cranelift and LLVM).
     Deferred: c-style `for init; cond; post`, `defer`, labeled loops.
   - *4-enum [done]* — **user enums with int/nullary payloads + `match`** in the
     backend-neutral IR (both backends; no backend changes needed). An enum is a
     heap block `[tag, p0, …]` — a struct layout in disguise — so construction,
     ownership, clone, and drop reuse `StructMake` / `StructField` /
     `StructClone` / `DropStruct` verbatim. New in `native_ir.rs`: variant
     construction (`Point`, `Circle(5)`) and `match`-as-expression lowering,
     which dispatches on the tag through an `Eq`/`Branch` decision chain, binds
     scalar payloads per arm, merges arm results through a stack slot (the IR has
     no block parameters), and — the memory-safety crux — drops an owned
     scrutinee exactly once on the taken arm (after its payload is read), so it
     neither leaks nor double-frees. Fixture `native_enums.mko` passes on
     Cranelift and LLVM (C-identical, 0 leaks under `leaks`, clean under
     GuardMalloc). Deferred: owned (string/slice) payloads (need per-variant
     recursive drop), generic enums (`Option[T]`/`Result[T,E]`), match guards,
     whole-scrutinee identifier patterns, and nested payload patterns.
   - *4-enum-owned [done]* — **enum owned (`string`) payloads** (Result-like,
     e.g. `enum Msg { Text(string), Code(int), Quit }`). Solved **without**
     tag-conditional clone/drop: each variant gets its own dedicated,
     non-overlapping payload slots, so every slot has one fixed type and the flat
     recursive `StructClone`/`DropStruct` is correct — the active variant's
     payload is cloned/dropped and inactive slots hold null. String clone is made
     **null-safe** in both runtime ABIs (drop already is), so the no-op on
     inactive slots needs no branch. A new `EnumMake` allocates a zeroed block
     (Cranelift `calloc`-backed `struct_make`; LLVM `calloc`) and stores the tag
     plus the variant's payload at its `slot_base`. `match` binds payloads from
     the variant's slots (results restricted to scalars — owned results still
     deferred, since a returned payload borrow would dangle at scrutinee drop).
     Fixture `native_enum_payload.mko` passes on both backends (C-identical, 0
     leaks, GuardMalloc-clean). Deferred: slice payloads, bool/float payloads,
     multi-owned-payload variants beyond string.
   - *3h [done]* — **nested aggregate fields** (struct-in-struct, recursive
     clone/drop). Aggregate layouts are built in two passes so a field may name
     another aggregate declared later. Both backends walk each layout
     recursively (null-safe): nested `Struct` fields re-enter the same
     per-layout clone/drop walk, and `[]string` fields/payloads are allowed.
     Fixture `native_nested_structs.mko` passes on Cranelift and LLVM
     (C-identical, 0 leaks, GuardMalloc-clean).
   - *4-enum-owned-result [done]* — **owned match results**
     (`match m { Text(s) => s }`). Heap arm results are cloned (if borrowed —
     typically a payload binding into the scrutinee) or moved (if already
     owned) into the merge slot **before** the scrutinee drops, so the result
     never dangles. Fixture `native_match_owned.mko` passes on both backends
     (C-identical, 0 leaks, GuardMalloc-clean).
   - *4-match [done]* — scalar `match` (int/bool scrutinee): literal arms,
     or-patterns (`1 | 2 | 3`), wildcard, exhaustive bool, and identifier
     catch-all that binds the scrutinee. Lowered as a linear decision chain with
     a merge block parameter; the last arm is the fallthrough (frontend guarantees
     exhaustiveness). Fixture `examples/native/native_match.mko`. Deferred: enum/
     variant/tuple/struct patterns, guards, and non-scalar scrutinees.
5. **Runtime interop** — call the precompiled runtime archive (net/db/tls/fmt/…) via
   the native ABI instead of libc-only.
6. **Concurrency** — `crew`/`kick`/`fan`/channels/`select`.
7. **LLVM release backend** — *in progress*: scalar CFG, owned strings, and `[]int`
   slices (including parameters, returns, append, slicing, and drops) are complete;
   nested ownership remains.
8. **Bundled lld + runtime archive** — *done for the current host slice*: lld and
   `runtime/native_runtime.c` are embedded and linked without SDK, `xcrun`, `cc`,
   or `ld` at program-build time.
9. **Build modes** — cross, wasm, static, sanitizers, overflow-checked arithmetic.

## Remaining work (tracked checklist)

Everything below is the **native backend** (shared IR + Cranelift + LLVM)
reaching parity with the mature C backend. Each item ships a positive
differential fixture and, where heap is involved, `leaks` + GuardMalloc coverage
before it counts as done.

**Shared IR handles today:** scalar CFG, `if`/`while`/`if init; cond`,
counted/`range` `for`, c-style `for`, `defer` (LIFO on exit/return), labeled
loops + labeled `break`/`continue`, owned strings (`len(s)`, `format_int`,
f-string interp for string/int/bool), `[]int`/`[]float`/`[]bool`, `[]string`
(both backends), structs (scalar / `string` / `[]int` / `[]string` /
nested-aggregate fields), tuples (+owned elements, +owned multi-return), enums
(int / nullary / `string` payloads) + `match` (enum + scalar int/bool, or-
patterns, guards, owned results, whole-scrutinee binding). `--overflow trap`
works on the native shared-IR path. All covered fixtures are 0-leak and
GuardMalloc-clean on both backends.

### Aggregates — remaining depth
- [x] Nested aggregate fields (struct-in-struct) via recursive per-layout
      clone/drop (null-safe). Fixture `native_nested_structs.mko`.
- [x] `[]string` fields (both backends). Remaining: slice-typed enum payloads;
      other slice fields (`[]float`/`[]bool` in structs).
- [ ] `bool`/`float` enum payloads; multi-owned-payload variants.
- [ ] Maps (`map[K]V`) — none in the shared IR yet.
- [ ] Methods (`on Type { fn m(self) }`) on native aggregates.
- [x] Owned `match` results (`match m { Text(s) => s }`). Fixture
      `native_match_owned.mko`.
- [ ] Generics — generic structs/enums (`Option[T]`/`Result[T,E]`), shared-IR
      monomorphization.

### Control flow — remaining
- [x] `for` (counted + range) and c-style `for` in the shared IR. Fixtures
      `native_for.mko`, `native_cfor.mko`.
- [x] `defer`, labeled loops + labeled `break`/`continue`. Fixtures
      `native_defer.mko`, `native_labeled.mko`.
- [x] Match: guards, scalar (int/bool) scrutinees, or-patterns, whole-
      scrutinee identifier binding. Fixtures `native_match.mko`,
      `native_match_guards.mko`. Remaining: deeply nested payload patterns.
- [ ] `if`-as-expression and block expressions (richer `match` arm bodies).
- [ ] `switch`/`case`.

### LLVM-specific gaps
- [x] LLVM `[]string` lowering (value ABI `mako_native_str_slice_*`).
- [ ] LLVM `[]float`/`[]bool` slices.

### Runtime interop
- [x] Seed: `format_int`, f-string interp (string/int/bool), `len(string)`.
      Fixture `native_fmt.mko`.
- [ ] Full precompiled runtime archive from native: networking, TLS, database,
      wider `fmt`/stdlib. Still only a handful of builtins beyond the seed.

### Concurrency
- [ ] `crew`, `kick`, `fan`, channels, `select` — none in the native path yet.

### Build modes
- [x] `--overflow trap` on the native shared-IR path (parity gate vs C).
- [ ] Cross-compilation (native is host-only), WASM, `--static`, sanitizers
      (`--sanitize`).

### Gates, infra & cleanup
- [x] macOS gate enables `--features llvm-backend` when the static lld
      toolchain is present (`scripts/native-compiler-test.sh`).
- [x] Runtime headers resynced via `scripts/install.sh --skip-build`; C oracle
      for slice programs works with `MAKO_RUNTIME` pointing at the checkout (or
      a resynced install).
- [x] Slice fixtures + new control-flow/fmt fixtures in the C-differential gate.
- [ ] Full latency / RSS / binary-size gates across C/native/LLVM (leak +
      correctness are covered; perf gates are partial — LLVM gate has compile/
      runtime/RSS/binary ratios).
- [ ] Confirm the whole suite on the Linux box (where `cc` linking + the C
      differential work without the feature build).

**Suggested order next:** runtime archive interop → concurrency → remaining
build modes → Linux confirmation.

## Roadmap to 100%

"100%" is defined by two **measurable gates**, not assertions. The granular
checklist above tracks features; this section tracks the milestones and the
gates that certify them.

### The two gates

1. **Syntax gate = full differential parity.** Every program the C backend
   accepts, the native backend (LLVM release + Cranelift debug) compiles and
   runs with byte-identical stdout/stderr/exit — across the whole
   `examples/testing` corpus (357+ programs) plus downstream (leba), 0
   differential failures, all memory-safe (0 leaks + GuardMalloc/ASan).
2. **Perf gate = leadership across a broad suite.** On a published multi-workload
   suite, native (LLVM) is ≤ C **and** ≤ Rust on wall-clock, CPU, **and**
   peak-RSS, with binary ≤ C — never worse than 5% on any single workload,
   faster on the majority. Every result records flags, hardware, samples,
   variance, RSS, alloc count, binary size, and compile time.

### Baseline (measured, Apple arm64, this checkout)

- **Syntax:** shared IR compiles ~15% of real programs (24/160 top-level
  examples); the dominant blocker is runtime/stdlib interop (~57% of rejections
  are `unknown call`), then maps/generics, then top-level items.
- **Perf (LLVM release, 7 samples):** faster-or-tied vs C and Rust on 3 of 4
  bench workloads (`fib`, `parity`, `string_slice`); the append-heavy `slice`
  workload is ~17% behind hand-C/Rust. Peak-RSS equal-or-better everywhere
  (0.22× on `fib`); binaries ~1.1× of C and 11–12× smaller than Rust.

### Milestones (syntax track)

- **M1 · Compute-complete** *(pure-compute programs; ~40% coverage)* —
  foundation is **per-type `clone`/`drop` functions** (nested composition falls
  out).
  - [x] Control flow: `for`/c-for/`defer`/labeled loops/`match` guards/owned
        match results/whole-scrutinee binding.
  - [ ] `if`-as-expression, block expressions, `switch`/`case`; deeply nested
        payload patterns.
  - [ ] Expressions: full f-strings, closures/lambdas + first-class fn values,
        `Convert`, compound/parallel assign.
  - [ ] Types: **maps** (`map[K]V`), **generics** (shared-IR monomorphization →
        `Option[T]`/`Result[T,E]`), `bool`/`float` enum payloads, remaining slice
        element/field kinds.
  - [ ] Top-level: `const`, **methods** (`on Type`), **interfaces + dynamic
        dispatch**, `extern` C, multi-file imports/packs, visibility.
  - **Exit:** native compiles every pure-compute program in the corpus; perf gate
    green on an expanded compute suite.
- **M2 · Interop-complete** *(the #1 coverage lever → ~80%)* — teach native
  codegen to call the **existing precompiled runtime archive** with correct
  ABI + ownership (not reimplement it): `fmt`/`print`, string/math/time/os/fs/
  collections/encoding, then networking/TLS/HTTP/database/crypto.
  - [x] Seed: `format_int`, f-string interp, `len(string)` (`native_fmt.mko`).
  - [ ] Full runtime/stdlib surface.
  - **Exit:** native compiles the majority of real programs; leba builds & runs
    through native.
- **M3 · Concurrency** — `crew`/`kick`/`fan`/channels/`select` wired to the
  existing runtime scheduler. **Exit:** concurrency corpus passes differential +
  race gates on native.
- **M4 · Build modes & targets → full parity** — sanitizers, `--static`,
  cross-compile, WASM (overflow-trap ✅). **Exit: syntax gate = 100%** (full
  corpus, all targets, 0 differential failures).

### Milestones (perf track, parallel)

- **P1 · Close the known gap** — the `slice`/`append` path (~17% behind hand-C):
  inline `append`/growth, elide bounds checks from ownership+range facts, drop
  redundant memcpy.
- **P2 · Cash in "better IR than clang/rustc"** — `noalias` from ownership,
  ownership/range bounds-check elision, **escape analysis → stack allocation**,
  guaranteed devirtualization, closure inlining, import-aware DCE, PGO.
- **P3 · Broaden the suite** — add string/hashing/JSON/map-heavy/alloc-heavy/
  HTTP-throughput/tail-latency/multi-core-scaling workloads with C + Rust
  baselines and published records. **Exit: perf gate = 100%.**

### Certification (durable gates, run in CI on every change)

- [ ] **Ratcheting coverage gate** — run the full C corpus through native each
      CI run; track pass %, never regress, until 100%.
- [ ] **Perf gate** on the broad suite with per-workload records + ratchet.
- [ ] **Cross-platform** — all gates on macOS-arm64 + Linux-x86_64 + ARM/RISC-V.
- [x] Local infra: install/runtime-header resync so the C oracle works for slice
      programs; slice fixtures back in the C-differential list.

### Sequencing

```
M1 compute ─► M2 interop ─► M3 concurrency ─► M4 modes  = SYNTAX 100%
        └─────────► P1 ─► P2 ─► P3                       = PERF 100%
(ratcheting coverage + perf gates certify each step; self-host fixed point
 per docs/SELF_HOSTING.md is a longer-horizon, optional M5.)
```

**Highest ROI to start:** M2 interop (largest coverage jump) and P1 (the one
perf gap) — they move both headline numbers fastest. The per-type clone/drop
generalization (start of M1) unlocks maps/generics/nesting and should land early.

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
always clone views rather than borrowing). String equality/comparison are lowered
by the embedded runtime; formatting remains deferred.

## Verification

**Memory safety is mandatory**, not optional. An increment is not done until:

1. GuardMalloc (macOS) / ASan (elsewhere) runs clean — no UAF, no double-free.
2. `leaks --atExit` reports **0 leaked bytes** on every heap-touching fixture.
3. C/native (and C/LLVM) differential stdout/stderr/exit match.

`./scripts/native-compiler-test.sh` enforces this for the full native fixture
set under `[4b/5]` (including `native_mem_stress.mko`, which exercises
continue/break over `[]string`, owned match results, defer, nested owned
fields, and f-strings). The LLVM gate applies the same GuardMalloc + leaks
bar to its differential fixtures.

`./scripts/native-compiler-test.sh` also covers Rust unit tests, the self-host
frontend gate, ownership regressions, and native/C differential execution.
The LLVM-specific gate is `scripts/llvm-backend-test.sh`.
