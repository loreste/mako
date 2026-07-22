# Mako native backend — shared IR specification

This document specifies the **backend-neutral native IR** (`src/native_ir.rs`):
the ownership-explicit intermediate representation that sits between the reused
Rust frontend and the two machine-code backends. It is the contract every
backend must honor.

```
.mko → frontend (AST) → native_ir::lower → Module
                                         ├── native_codegen::compile_ir  (Cranelift, debug)
                                         └── llvm_codegen::compile_ir     (LLVM, release)
```

Scope: this is a living spec for the native compiler foundation
(product 0.4.5). It complements — does not replace —
[docs/NATIVE_COMPILER_PLAN.md](docs/NATIVE_COMPILER_PLAN.md) (increment ladder),
[docs/SELF_HOSTING.md](docs/SELF_HOSTING.md) (bootstrap contract), and
[docs/MEMORY_MODEL.md](docs/MEMORY_MODEL.md) (language memory model).

## 1. Design principles

1. **Ownership is resolved in the IR, not in a backend.** Heap allocation,
   clone, move, and drop are explicit instructions. A backend that preserves
   them is correct by construction; it never re-derives ownership.
2. **Backends agree on observable behavior, not on layout.** A program is
   compiled by exactly one backend, so internal representations (e.g. struct
   field byte offsets) may differ. Only stdout/stderr/exit status and
   allocation discipline (no leaks, no double-frees) must match the C backend.
3. **Unsupported constructs are hard errors.** `native_ir::lower` returns `Err`
   rather than silently narrowing semantics. `MAKO_NATIVE_SHARED_IR_ONLY=1`
   turns the AST-path fallback off so tests fail loudly on regressions.
4. **Every increment ships a positive differential fixture** (native vs C:
   stdout/stderr/exit) and, where heap is involved, a leak/GuardMalloc fixture.

## 2. Types

`native_ir::Type` (all `Copy`):

| Type | Meaning | Heap-owned |
|------|---------|:---------:|
| `I1` | `bool` | — |
| `I32` | narrow int | — |
| `I64` | `int` / `int64` | — |
| `F64` | `float` | — |
| `Str` | owned `MakoString` (`data,len`) | yes |
| `IntSlice` | owned `[]int`/`[]float`/`[]bool` (`data,len,cap`) | yes |
| `StrSlice` | owned `[]string` (recursive clone/drop) | yes |
| `Struct(id)` | user struct, `id` indexes `Module::structs` | yes |

`Type::is_heap()` is the single predicate that drives clone/move/drop. Adding a
new owned type means: add the variant, make `is_heap` return `true`, and give
`emit_drop` / `emit_clone` a case.

## 3. Ownership model

Each heap value flows through lowering as `(Value, Type, owned: bool)` where
`owned` means "this SSA value is a fresh owner the consumer may take". Locals
additionally carry a static `heap_owned` flag.

- **Literals / borrows** → `owned = false` (views, params, `Ident` reads).
- **Producers** (concat, `make`, `append`, slice-of-owned, struct literal,
  struct update, owning call return) → `owned = true`.
- **Scope exit** (every `return` and fallthrough) drops all still-owned locals.
- **`?` / branch / loop exits** must leave the owned-set unchanged, else lower
  rejects (conservative but sound).

### Move types vs copy types

- **`Str` / `IntSlice` / `StrSlice` are move types.** Binding a bare owned local
  (`let b = a`) transfers the buffer and clears the source's `heap_owned`.
- **`Struct` is a copy type.** Binding, assigning, or passing a *borrowed*
  struct by value emits `StructClone`, and both names stay live. Returning a
  bare owned struct still *moves* it out (the function is ending, so no aliasing
  is observable).

Borrowed heap values that must escape (owning return, by-value argument) are
cloned via `emit_clone`; owned temporaries are taken directly and dropped after
their last use via `emit_drop`.

## 4. Structure

- `Module { functions: Vec<Function>, structs: Vec<StructLayout> }`
- `StructLayout { name, fields: Vec<(String, Type)> }` — fields in declaration
  order; slot `i` is the `i`-th field. Scalar fields only in the current
  increment (`I1`/`I32`/`I64`/`F64`); string/slice/nested-struct fields are
  deferred (they need element drops + nested layout).
- `Function { name, params, ret, blocks, entry, next_value }` — SSA `Value`s;
  locals are `Alloca` + `Load`/`Store`; control flow is basic blocks with
  `Jump` / `Branch` / `Return` terminators.

## 5. Instruction set (heap + aggregate)

Strings: `StringLiteral`, `StringClone`, `StringConcat`, `StringEqual`,
`PrintString`, `DropString`.
Slices (`Int`/`Str`): `Slice{Literal,Make,Len,Index,Store,Append,Slice,Clone}`,
`DropSlice` / `DropStrSlice`.
Structs:

| Instruction | Semantics |
|-------------|-----------|
| `StructMake { out, struct_id, fields }` | allocate an owned block; store `fields[i]` into slot `i`; `out` = pointer |
| `StructField { out, base, struct_id, index, ty }` | load scalar field `index` |
| `StructFieldStore { base, struct_id, index, value }` | store scalar field `index` (mutation) |
| `StructClone { out, base, struct_id }` | deep-copy the block (value-copy semantics) |
| `DropStruct { value }` | free the block |

### Backend lowering contract for structs

- **Cranelift** (`native_codegen::compile_ir`): a struct pointer is `I64`;
  fields occupy uniform 8-byte slots at offset `index*8`. `StructMake`/`Clone`/
  `Drop` call `mako_native_struct_{make,clone,drop}_ptr`
  (`runtime/native_runtime.c`, `calloc`/`memcpy`/`free`); field access is inline
  typed `load`/`store` at the slot offset.
- **LLVM** (`llvm_codegen::compile_ir`): a struct pointer is an opaque `ptr`; a
  concrete LLVM struct type per `id` gives natural field offsets. `StructMake`/
  `Clone` use `malloc` + `getelementptr` (+ `llvm.memcpy` for clone); `DropStruct`
  uses `free`.

## 6. Backend dispatch (CLI)

`mako build|run --backend {native,llvm}` routes through `build_native_object`:

- `native` (default debug): try `native_ir::lower` → `native_codegen::compile_ir`
  (Cranelift). Aggregates outside the shared IR fall back to the mature AST
  lowering unless `MAKO_NATIVE_SHARED_IR_ONLY=1`.
- `llvm` (release only): `native_ir::lower` → `llvm_codegen::compile_ir`. There
  is **no** AST fallback, so the shared IR is the sole path — every feature must
  live here to reach the optimizing backend.

Linking: bundled `lld` on macOS (behind the `llvm-backend` feature, using the
embedded runtime archive; no SDK/`xcrun`/`cc`), and the system `cc` on Linux.

## 7. Verification

- `cargo test` — IR unit tests (lowering shape, ownership instruction counts).
- `./scripts/native-compiler-test.sh` — self-host frontend gate, ownership +
  GuardMalloc/leak regressions, and C/native differential execution. Fixtures in
  the `MAKO_NATIVE_SHARED_IR_ONLY=1` list must lower through the shared IR.
- `./scripts/llvm-backend-test.sh` — LLVM shared-IR tests, C/LLVM differential
  output, SDK/PATH independence, and unsupported-feature rejection.

New backend features are not "done" until they extend the differential fixture
set and (for heap features) the leak coverage.

## 8. Status & open work

Done in the shared IR (both backends): scalar CFG, owned strings, `[]int`/
`[]float`/`[]bool`, `[]string` (Cranelift pointer ABI + LLVM value ABI),
**structs**, **tuples** (interned as anonymous positional structs; scalar and
owned `string`/`[]int` elements, including owned multi-return destructuring),
**enums with `match`** (int, nullary, and owned `string` payloads; each variant
gets dedicated non-overlapping slots so clone/drop stay flat and null-safe —
an enum is a `[tag, payload…]` heap block reusing the struct machinery; `match`
dispatches on the tag and drops an owned scrutinee exactly once per arm;
owned match results clone/move the payload before that drop), and **owned +
nested aggregate fields** (`string` / `[]int` / `[]string` / nested structs —
`StructClone`/`DropStruct` recurse per layout, null-safe).

Open (tracked in [docs/NATIVE_COMPILER_PLAN.md](docs/NATIVE_COMPILER_PLAN.md)):

- Slice-typed enum payloads; `bool`/`float` multi-owned enum payloads.
- Maps, methods, generics (`Option[T]`/`Result[T,E]`).
- Deeply nested match payload patterns; `if`-as-expression / block expressions.
- Full runtime archive interop (net/tls/db/wider fmt); `crew`/`kick`/`fan`/channels/`select`.
- Cross/WASM/static/sanitizer build modes (overflow trap is done on native shared-IR).
- LLVM `[]float`/`[]bool`.
- (All known native-backend leaks are now fixed.) Historical notes: the
  string-literal leak was fixed by emitting literals as static `MakoNativeString`
  views (no malloc, no drop); the slice-view header leak was fixed by returning
  an owned handle from slicing a borrowed base (the runtime frees the header but
  not the shared data, gated by the header's `owned` flag); and an owned
  string/slice temporary argument is now passed as a borrow and dropped after
  the call rather than cloned into a leaked copy. Every native fixture is 0 leaks
  and GuardMalloc-clean on both backends.
