# Full self-hosting execution plan

Branch: `selfhosted`

This is an implementation plan, not a completion claim. The acceptance
contract is defined by `FULL_SELF_HOSTING_MANDATE.md`; the current status is
also summarized in `SELF_HOSTING.md` and the staged roadmap in
`SELF_HOSTING_ROADMAP.md`.

## Current truth

Stage 0 is the Rust compiler in `src/`. It can compile the Mako sources in
`compiler/*.mko` through the existing C/native development backends. The
Mako-written compiler currently provides a lexer, structural parser, type
parser, symbol table, name/local resolution, primitive semantic typing, and a
verified typed-IR bring-up. Its executable entry point is still a frontend and
IR smoke-test driver: it prints arena statistics and does not compile a source
program into stage 2.

The self-host gate proves stage-0 → stage-1 generation plus stage-1 frontend,
IR, ELF reproducibility, and stage-0 differential checks for the supported
subset. It does not yet prove stage-1 → stage-2 bootstrap, a Mako-native
mandatory runtime, or a forbidden-tool dependency audit for stage 1+.

The current direct-backend slice covers scalar values, calls, branches, loops,
printing, integer arithmetic, checked byte conversion, unary integer
negation/complement, a direct Linux `linux_syscall6` intrinsic, explicit unsafe
byte-pointer load/store primitives, and explicit
ownership drops for static strings. `compiler/elf64.mko` serializes these
shapes into deterministic one-segment ELF64 images. The generated image is
written through the temporary stage-0 `write_file` builtin; Linux x86-64 gates
execute it, while Darwin/arm64 gates validate the ELF format and defer
execution.

`compiler/argv_native.mko` supplies the Mako-native side of that boundary:
byte-slice allocation and growth, owned string objects, argv access, and
file open/read/write/close, all emitted as direct Linux syscalls. Every one
of these is executed on Linux x86-64 by the `selfhost-linux` CI job. What
remains for the dependency-removal milestone is teaching stage 1 to lower
`main.mko`'s own `read_file`/`write_file`/`arg_get` calls onto these
primitives, so the compiler's I/O no longer routes through stage-0 builtins.

## Capability inventory

| Area | Current state | Evidence | Required next work |
|---|---|---|---|
| Lexer/spans | Partial | `compiler/lexer.mko` | Complete token/error parity corpus |
| Parser/AST | Partial | `compiler/parser.mko`, `z_expression.mko`, `z_statement.mko`, `z_type.mko` | Declarations, imports, patterns, generics, recovery |
| Symbols/resolution | Partial | `zz_symbols.mko`, `zzz_resolution.mko`, `zzzz_locals.mko` | Packages, visibility, generic/interface resolution |
| Type checking | Partial | `zzzzz_types.mko` | Full aggregates, conversions, constants, interfaces |
| Ownership/NLL | Partial | frontend checks plus IR ownership verifier | CFG-wide borrow/escape/definite-init/send analysis |
| Typed IR | Partial | `zzzzzz_ir.mko` | General loops, break/continue, match/select/defer, closures, aggregates |
| Optimization | Unsupported | none in `compiler/*.mko` | Deterministic verifier-preserving pass pipeline |
| x86-64 encoding | Partial bring-up | `compiler/x86_64.mko` encodes scalar CFGs, calls, loops, print, and integer arithmetic | complete ABI, register allocation, relocations, floats, aggregates |
| Register allocation | Unsupported | no Mako implementation | Linear scan, spills, stack slots |
| ELF/object writer | Partial bring-up | `compiler/elf64.mko` writes deterministic one-segment ELF64 images | sections, symbols, relocations, static layout, malformed-object rejection |
| Linker | Unsupported | host linker used by stage 0 | Mako static symbol resolution and executable layout |
| Runtime | C/Rust boundary | `runtime/`, Rust native bridges, stage-0 builtins | Mako-native syscalls, allocator, strings, collections, tasks, I/O |
| Driver/bootstrap | Rust + shell | `src/main.rs`, `scripts/selfhost-gate.sh` | Mako-native build/check/bootstrap and stage fixed point |

## Dependency graph and removal order

```text
Mako language foundations
  -> Mako frontend and diagnostics
  -> ownership/NLL/concurrency analysis
  -> verified general SSA IR
  -> deterministic optimization
  -> x86-64 ABI/lowering/register allocation
  -> machine encoding and relocations
  -> ELF64 writer/static linker
  -> Mako Linux runtime
  -> Mako driver and bootstrap
  -> stage 0 -> stage 1 -> stage 2 -> stage 3 fixed point
```

Stage 0 may continue using Rust, C, Clang, Cranelift, LLVM, and the system
linker as temporary oracles. Each dependency must be removed from the stage-1+
build and runtime path before the bootstrap milestone can pass. No unsupported
Mako construct may silently select a legacy backend.

## Language expansion required by the compiler

The compiler and binary writer require the following language/runtime work,
implemented in stage 0 and then used by the Mako compiler with differential
tests:

1. Fixed-width integers/floats, checked and wrapping arithmetic, defined
   shifts, casts, byte swaps, and bit operations.
2. Fixed arrays and byte slices with capacity-aware append, endian reads/writes,
   copying, hashing, bounds checks, and explicit ownership/view behavior.
3. Stable layout controls: representation, packing, alignment, size/offset,
   pointer width, endianness, and stable enum/tag layouts.
4. Audited raw pointers and unsafe load/store/copy/compare operations.
5. Direct Linux x86-64 syscall intrinsic, atomics, memory ordering, and typed
   function pointers/calling conventions.
6. Bounded deterministic `const fn`, compiler collections, typed diagnostic
   chains, target configuration, reproducible-build primitives, and modules.

Every item needs positive and negative stage-0 tests, Mako implementation,
stage-0/stage-1 differential tests, and bootstrap-corpus coverage.

## Performance and concurrency acceptance

The self-hosted implementation must preserve Mako's systems-language goals while
removing foreign-toolchain costs. Compiler work is therefore measured in both
cold and incremental builds, with tokenization, parsing, type checking, IR,
code generation, peak memory, and output size recorded separately. The design
requires arena-backed immutable compiler data, bounded iterative passes,
incremental fingerprints with dependency-aware invalidation, parallel function
compilation, and deterministic merge/order so parallelism never changes the
artifact.

The native runtime and generated programs must also be measured for wall time,
CPU time, peak RSS, allocations, synchronization overhead, and tail latency.
Concurrency features must use explicit ownership and synchronization semantics;
they cannot trade safety for throughput. The Mako release backend must beat or
tie the main-branch C/Rust baselines per published workload, while the debug
backend must minimize compile latency. No benchmark result is accepted without
reproducible flags, hardware, sample count, variance, and binary size.

## Milestones

### Milestone 0: truth and parity infrastructure

Implemented:

- Existing stage-0 → stage-1 frontend gate and negative fixtures.
- This execution plan and the existing mandate/roadmap.

Files changed:

- `docs/FULL_SELF_HOSTING_EXECUTION_PLAN.md`

Tests added:

- None; existing gates are the baseline.

Commands run:

- `rtk cargo test --bin mako native_codegen`
- `rtk proxy bash scripts/selfhost-gate.sh`

Results:

- Native unit subset: 12 passed.
- Self-host frontend gate: passed.

Legacy dependency removed: None.

Remaining dependency: Stage 0 Rust compiler, C/native backend, host linker.

Known limitations: Stage 1 cannot compile stage 2.

Next milestone: Mako-native compiler runtime primitives and the first explicit
stage-1 → stage-2 bootstrap build for a supported compiler subset.

### Milestone 1: complete executable IR subset

Implement general straight-line and CFG lowering for integer/bool operations,
returns, calls, branches, loops, and explicit ownership cleanup. Add malformed
IR verifier fixtures for dominance, terminators, type mismatches, and ownership
paths. The lowerer must reject unsupported constructs with source locations.

Acceptance:

- Stage-0/stage-1 IR summaries match for the bootstrap corpus.
- Every emitted IR function passes structural and ownership verification.
- Positive and negative fixtures cover each newly supported operation.

### Milestone 2: direct Linux x86-64 backend

Implement a Mako machine-instruction representation, System V AMD64 lowering,
linear-scan allocation, stack frames, branches, calls, integer/floating scalar
operations, static data, and direct byte encoding. No assembly text is emitted.

Acceptance:

- A Mako-written compiler emits executable bytes for a constant-return and
  hello-world program.
- Linux x86-64 execution matches the stage-0 oracle.
- Unsupported IR fails explicitly.

### Milestone 3: ELF64 and static linking

Implement deterministic ELF64 object/executable structures, symbol tables,
relocations, section/segment layout, entry-point generation, and static symbol
resolution in Mako. Add malformed-object rejection and reproducibility tests.

Acceptance:

- `mako-independent build hello.mko -o hello` works without assembler or host
  linker.
- Rebuilding identical inputs produces identical normalized ELF bytes.

### Milestone 4: Mako-native Linux runtime

Move mandatory startup, syscalls, allocator, strings, slices, maps, files,
panic diagnostics, and resource cleanup into Mako. Add channels, tasks,
futex-based synchronization, and cancellation only after the scalar runtime is
stable.

Acceptance:

- Hello, file I/O, maps/slices, and compiler execution work without libc or C
  runtime code.
- Allocation census, guard-page, double-free, and leak tests pass.

### Milestone 5: bootstrap fixed point

Implement `mako bootstrap` and its Mako-native equivalent driver. Build stage 1
from stage 0, stage 2 from stage 1, and stage 3 from stage 2; run the same
positive/negative corpus and compare diagnostics, behavior, and normalized
artifacts. Trace processes and reject forbidden tools in stage 1+.

Acceptance:

- Stage 1 and stage 2 are behaviorally equivalent.
- Stage 2 and stage 3 are reproducible under documented normalization.
- A clean Linux x86-64 environment needs no foreign compiler, assembler,
  linker, or foreign-language runtime to build and run Mako.

## Risks

- The current plain `int` and dynamic arrays are insufficient for correct ELF
  and syscall encoding; fixed-width bytes/layout must precede production output.
- Ownership rules for compiler buffers, views, and memory mappings can create
  unsound code if added only in the backend.
- A stage-1 frontend executable is not evidence of a stage-1 compiler; every
  milestone must include executable stage-1 output or remain explicitly partial.
- Runtime migration may expose undocumented C ABI behavior in existing examples.
- Reproducibility can be invalidated by source ordering, timestamps, paths,
  hash iteration, allocator addresses, or host metadata.

## Complexity estimate

This is a compiler/runtime rewrite spanning all major language layers. The
work is expected to require many independently reviewable milestones; no date
or single-turn completion estimate is credible. The definition of done remains
the mandate’s full stage-1/stage-2/stage-3 and clean-machine contract.
