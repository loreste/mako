# Self-hosting progress snapshot

This is an evidence log, not a completion claim.

## Current checkpoint

- Branch: `selfhosted`.
- The Mako-written stage-1 driver emits deterministic static ELF64 x86-64
  images directly, without generating C or invoking an assembler/linker for the
  emitted image.
- The native boundary currently covers scalar control flow, direct syscalls,
  byte slices, argv access, owned strings, file descriptors, open/read/write,
  and retrying whole-buffer I/O.
- Generalized sequential `if` lowering now works. A body that is an arbitrary
  chain of `if cond { print("..."); exit(n) }` guards followed by a tail lowers
  through the shape-independent if-tree path — no matched shape, no block-count
  pattern — and encodes through the general returning-CFG encoder.
- `cargo test --bin mako native_codegen`: 12 passed.
- `scripts/selfhost-gate.sh`: passed on Darwin/arm64 after the superlinear-path
  and make_byte work below (frontend pins, ELF image pins, negatives; ELF
  execution still deferred to Linux x86-64).

## What the last increment added

Four boundaries moved, each with a pinned fixture and executed evidence:

- **Result-less functions and process exit.** The general if-tree lowerer
  accepts `fn main()` with no declared result, and treats `exit(n)` as a path
  terminator (drops, `IR_EXIT`, then a value-less `IR_RETURN` so the block has
  a structural terminator).
- **String constants outside the matched shapes.** `IrCfgLowering` carries
  `string_literals` and a `constant_string_index` table kept index-parallel
  with `constants`. It was previously possible for the general path to append a
  constant without an index entry, which would have made a later string
  constant address the wrong literal; the two arenas can no longer drift.
  A string constant is admitted only at a node whose owner consumes it (a print
  argument, a let initializer, a returned value) and fails closed elsewhere, so
  the ownership verifier's "consumed exactly once" rule still holds on every
  path.
- **Bool-returning calls and logical `not`.** `if not check()` lowers. `not` on
  a bool emits `test/sete/movzx`, not the integer complement — complementing 1
  gives -2, which every truth test still reads as true.
- **More than thirty-two blocks per function.** Dominator sets are packed
  bitsets (32 bits per word, sized by the widest function) instead of one
  machine int, and the general encoder's slot ceiling is a documented 4096-value
  frame bound rather than the fixed shapes' 127.

Evidence: `elf_guard_chain_exit`, `elf_bool_guard_exit`, and
`elf_long_guard_chain_exit` are pinned in the gate by arena shape and image
hash, checked against the stage-0 oracle for stdout and exit status, and
executed on Linux x86-64 by the `selfhost-linux` job. Both arms of the bool
guard and a mid-chain arm of the forty-one-block chain are covered, so a branch
reaching a block far from the entry is proven, not assumed.

## Measured blocker: the frontend/IR path is still superlinear

This remains the binding constraint, ahead of any remaining syntax. Work since
the previous snapshot attacked the known quadratic sources; the gate still
passes, but large sources are not yet linear.

### Fixes landed (evidence, not a completion claim)

- **Append-only expression/type/statement arenas.** Recursive parse returns
  move the arena instead of `clone_*` on every node. Stage 0 models partial
  field moves well enough for "read scalars, then take `nodes`".
- **Attach path.** Failed expression attachment truncates back to the prior
  length so partial parse nodes cannot inflate later passes (restored the
  `zzzzz_types.mko` expression count to 4040).
- **Per-item indexes in `lower_typed_ir`.** Statements, expressions, and locals
  are partitioned once by owning function; the hot shape and lowering loops
  walk those lists instead of re-scanning the whole arena per function.
- **Local resolution.** One token→body map and one pass over expressions,
  instead of `O(bodies × expressions × locals)`.
- **Initializer binding.** `initializer_root → local` map replaces scanning
  every local for every lowered expression.

### Still superlinear (remeasured Darwin/arm64 after the fixes)

| Workload | size | time |
|---|---|---|
| many `exit(i)` functions | 800 fns | ~1.8 s |
| many `exit(i)` functions | 1600 fns | ~7.2 s (~4×) |
| straight-line `let x = i` | 2000 lets | ~1.0 s |
| straight-line `let x = i + 1` | 2000 lets | ~9.6 s |
| `compiler/zzzzz_types.mko` | 4040 exprs | ~4.2 s |
| `compiler/zzzzzz_ir.mko` | ~26k exprs | ~160 s |

Doubling function count still ~4× wall time. Shape-match *failure* on a large
body can cost *more* than successful lowering (2000 lets + `while false` skip
~3.2 s vs successful lower ~1.0 s). Remaining cost is still in whole-arena
work: statement collection per body, shape probes that walk all locals, and
likely instruction/constant growth in the C-backed stage-1 runtime.

The practical consequence is unchanged: a Linux x86-64 host with 8 GB RAM can
still OOM on the largest sources, and a stage-1 → stage-2 fixed point over the
full compiler package is blocked until this is linear enough to finish.

## Memory findings fixed this increment

Both were leaks, not crashes, and both are in the owned-string primitives:

- `unsafe_read_file` mmaps the whole file and returns it, but
  `x86_string_value_owned` listed only `arg_get` and `unsafe_bytes_to_string`
  as producing owned strings, so the `IR_DROP` the lowering already emits
  compiled to nothing. Every call leaked the entire file buffer. The IR's
  consumed-exactly-once rule is what makes enabling the drop safe.
- `x86_emit_drop_owned_string` munmapped `length + 8` while every producer maps
  `length + 10` — the header word, the payload, a NUL and a newline. Whenever
  `length + 8` landed exactly on a page boundary the final page stayed mapped.

Both are verified only by the pinned images still producing correct output.
Neither has the loop-under-a-leak-checker test the memory-safety bar asks for:
that needs a loop calling the primitive thousands of times, and loops in this
path are one of the blockers listed below. This is a known gap, not a claim of
leak-freedom.

## Remaining blockers to the next bootstrap fixed point

- Stage 1 does not yet rebuild the complete compiler source into stage 2.
- `compiler/main.mko` still stops at the driver's explicit `ir.skipped_functions`
  guard, but the boundary has moved past its whole guard prologue: the same
  thirty-four-guard chain with stubbed checks lowers to 103 blocks with zero
  skipped functions and verifies. The remaining constructs in `main`'s tail,
  each reproduced by a minimal probe, are:
  - **String concatenation**, which needs an owned temporary with a drop, and
    `format_int`, which has no backend emitter at all.
  - **`while` loops inside the general if-tree**, which needs block parameters
    and therefore a real merge, not the parameter-free tree the path relies on.
  - **Cloning an owned string.** Passing an owned string binding to a call emits
    `IR_CLONE`, which the encoder implements for slices but not strings. That is
    what stops `unsafe_read_file(path)` after `let path = arg_get(1)`. It is not
    a mechanical addition: an owned string is an mmap'd `{length, bytes}` block
    while a literal is a static view in the code image, and the encoder decides
    which by inspecting the producing instruction. A clone has to preserve that
    distinction or a later drop munmaps read-only code.
  - Struct field access and slice/array operations, which the general path does
    not model at all.
- ELF execution is not run on the Darwin/arm64 development host; images are
  format- and hash-checked there and executed on Linux x86-64.
- The Rust stage-0 compiler and the compatibility C runtime remain required
  during bring-up. Removing them is not yet complete.

The next implementation boundary is the frontend's complexity above. The
language features below are real but secondary: shipping them onto a compiler
that cannot process its own sources does not move the bootstrap.

## Adversarial QA (Darwin/arm64)

Evidence only; not a self-host claim.

- Gate green; literals/typed_ir pins match; sample ELF fixtures emit with
  `ir_skipped 0`; ≥45 negative fixtures fail closed with diagnostics; no
  segfaults under empty/garbage/huge-id/deep-nest abuse.
- **Wired into the gate:** stage-0 checks for `x86_64.mko`, `elf64.mko`, and
  `argv_native.mko`; stage-1 frontend smoke for `argv_native.mko` and
  `elf64.mko` (in addition to the existing frontend unit list). `make selfhost`
  runs `scripts/selfhost-gate.sh`. CI `selfhost-linux` and
  `scripts/native-compiler-test.sh` still invoke the same gate.
- **Not stage-1 single-file smoke:** `compiler/x86_64.mko` fails stage 1 alone
  with `IR call targets a function without an IR body` (~163 s) until multi-file
  packages land. It is still typechecked and linked when stage 0 builds
  `main.mko`, and `x86_64_smoke_test` (including the returning-CFG smoke) runs at
  stage-1 process start.
- `main.mko` still cannot emit ELF (`ir.skipped_functions` / unsupported tail).
- Fail-open I/O: missing path, broken symlink, and directory path exit 0 with
  empty-arena stats (look like success). Stats-only mode on `bad_print_*` also
  looks green (`ir_skipped 1`); the gate rejects those only when an output path
  is requested.

## Security review (static, white-hat)

- Confirmed fixed: `unsafe_read_file` in the owned-string drop set; munmap size
  matches map size (`length + 10`).
- Fixed this increment: `x86_emit_make_byte` no longer silently no-ops on bad
  length (caller rejects and treats unchanged buffer as unsupported). Was
  fail-open: result slot left uninitialized.
- Still open: owned-string `IR_CLONE` (H1 / bootstrap blocker); slice grow/clone
  size overflow checks; mmap/open failure paths; `[]byte` ownership policy;
  type negatives for more unsafe ops; leak soaks under ASan on Linux.
- Solid: ownership path verifier, intrinsic arity negatives in the gate,
  saturated decimal IR parse, fail-closed unsupported lowering (with the
  make_byte exception closed).
