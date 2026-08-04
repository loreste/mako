# Full self-hosting mandate

Status: planning contract only. No implementation work is authorized by this
document.

This document records the acceptance contract for the `selfhosted` branch. It
is derived from `MAKO_FULL_SELF_HOSTING_CODEX_MANDATE.md` and is intentionally
kept in the repository so future implementation work is reviewable and does
not depend on a file in a developer's Downloads directory.

## Definition of done

Mako is fully self-hosted only when all of these are true:

1. Stage 0 compiles the Mako compiler sources into stage 1.
2. Stage 1 compiles the same sources into stage 2.
3. Stage 2 compiles the same sources into stage 3, or rebuilds the agreed
   verification corpus.
4. Stage 1 and stage 2 have equivalent behavior.
5. Stage 2 and stage 3 are byte-identical after documented build-metadata
   normalization, or every remaining difference has a reproducibility
   explanation.
6. Stage 1 and later do not invoke, load, or emit code for Rust, Cargo, C,
   Clang, GCC, LLVM, Cranelift, Zig, an assembler, or a system linker.
7. A clean Linux x86-64 machine containing only the bootstrapped Mako
   toolchain and its source/runtime bundle can compile and link a Mako program.

Wrapping the Rust compiler, emitting C, or invoking LLVM/Cranelift/Clang does
not satisfy this contract.

## First independent target

The first fixed point is Linux x86-64 producing statically linked ELF64
executables. The compiler must encode machine instructions and ELF structures
directly. Other operating systems and formats follow only after this target is
reproducibly bootstrapped.

## Final dependency boundary

The final compiler and mandatory runtime may depend on Mako source, checked-in
data tables, Linux system calls, documented ABI/format specifications, and a
previously bootstrapped Mako seed. The required build path may not depend on a
foreign compiler, generated C, external assembler/linker, foreign-language
generator, shell interpreter, or precompiled library containing compiler logic.

Shell scripts and the current Rust/C implementation remain permitted as
temporary stage-0 development tools and behavioral oracles. They must never be
an invisible stage-1 fallback.

## Required production pipeline

```text
.mko source
  -> lexer/parser and package resolver
  -> typed AST and semantic/safety analysis
  -> verified target-independent typed SSA IR
  -> deterministic optimization
  -> x86-64 instruction selection and register allocation
  -> machine-code encoding and relocations
  -> ELF64 writer and static symbol/link resolution
  -> standalone Linux executable
```

Production components that must eventually be written in Mako include the
frontend, ownership/NLL/concurrency analysis, typed IR and verifier,
optimization passes, x86-64 backend, ELF/object writer, static linker logic,
bootstrap driver, and all mandatory runtime behavior.

## Required language and runtime foundations

The implementation plan may extend Mako where the compiler cannot otherwise be
expressed. The minimum planned foundation is:

- fixed-width integers/floats, checked/wrapping arithmetic, and defined shifts;
- fixed arrays, byte slices, endian-aware reads/writes, copying, hashing, and
  deterministic collection behavior;
- stable layout controls (`repr`, packing, alignment, size/offset queries,
  tags, unions, endianness, and pointer width);
- typed raw pointers and audited unsafe load/store/copy/compare operations;
- native Linux syscalls, atomics and memory ordering, function pointers, and
  stable calling conventions;
- bounded deterministic `const fn` evaluation;
- compiler collections, typed diagnostic chains, resource-safe APIs, target
  configuration, reproducible-build primitives, and compiler-suitable modules.

Every language addition must be specified, tested positively and negatively,
implemented in stage 0 as a bootstrap step, implemented by the Mako compiler,
covered by stage-0/stage-1 differential tests, and included in the bootstrap
corpus. Safety, unsafe, target-specific, and implementation-defined behavior
must be recorded.

## Required product commands

The self-hosted product must provide `mako check`, `build`, `run`, `test`,
`fmt`, `pkg`, `bootstrap`, `version`, and `doctor`. The LSP, debugger,
profiler, registry publishing, cross-compilation, and additional platforms are
not prerequisites unless later proven necessary for the Linux fixed point.

## Compatibility and quality gates

Stage 0 and each self-hosted stage must be compared on positive and negative
corpora for output, diagnostics, exit status, source locations, and observable
side effects. Existing syntax, ownership/NLL rules, overflow and bounds
semantics, packages, concurrency, standard-library behavior, and supported
interop must not change silently.

Performance claims are workload-specific and must record compiler versions,
flags, hardware, warmup, samples, variance, memory, allocations, binary size,
and compile time. Safety checks may only be bypassed by an explicit existing
unsafe operation.

See [the execution roadmap](SELF_HOSTING_ROADMAP.md) for staged work and
[the existing self-hosting status](SELF_HOSTING.md) for current bring-up
evidence. Neither document may claim the self-hosting definition above until
the bootstrap gates pass.
