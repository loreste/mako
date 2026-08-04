# Full self-hosting execution roadmap

This is the implementation planning document for the full self-hosting
mandate. It is a roadmap, not evidence that the work is complete. The initial
target is Linux x86-64 and static ELF64; all work should preserve the existing
Rust/C implementations as stage-0 oracles until the fixed point is proven.

## Phase 0 — establish truth

- Inventory the current language, compiler, runtime, and foreign dependencies.
- Record every feature used by `compiler/*.mko`.
- Maintain the capability matrix below with explicit `implemented`, `partial`,
  and `unsupported` states.
- Define stage-0/stage-1 and stage-1/stage-2 differential infrastructure for
  successful builds, failures, diagnostics, exit status, and program behavior.
- Freeze the bootstrap corpus, artifact normalization rules, dependency policy,
  and the Linux x86-64 acceptance environment.

## Phase 1 — Mako frontend

Complete lexer, parser, source spans, AST, imports/packages, visibility,
resolution, symbols, generics, interfaces, patterns, constants, type checking,
and recoverable deterministic diagnostics. The acceptance gate is equivalent
stage-0 and Mako-written frontend results over the complete positive and
negative language corpus.

## Phase 2 — typed IR and safety

Implement general CFG/SSA lowering, block parameters, calls, closures,
aggregates, loops, `match`, `select`, `defer`, `?`, structured concurrency,
ownership moves/clones/drops, NLL/escape analysis, definite initialization,
bounds and overflow semantics, Send/synchronization checks, and an IR plus
ownership verifier. Unsupported input must be rejected, never miscompiled or
silently routed to another backend.

## Phase 3 — deterministic middle end

Add constant folding, dead-code elimination, copy propagation, simplification,
justified bounds-check elimination, escape-aware stack allocation, a documented
inlining policy, deterministic pass ordering, and reproducible serialization.
Each pass needs verifier coverage and stage differential tests.

## Phase 4 — direct Linux backend

Implement x86-64 instruction selection, calling conventions, machine
instructions, linear-scan register allocation, spills, stack frames, branch
relaxation, constant pools, encoding, symbols, relocations, and source metadata.
The backend emits bytes directly; it does not emit assembly text.

## Phase 5 — binary production

Implement Mako ELF64 writing, section/segment layout, static symbol resolution,
relocations, entry-point generation, dead-section elimination, reproducible
metadata, and standalone static executable generation. Add relocatable-object
support when the architecture requires it. Later Mach-O and PE/COFF work is
out of scope for the first fixed point.

## Phase 6 — native runtime

Move mandatory runtime behavior into Mako: `_start`, process arguments and
environment, Linux syscalls, allocator, strings, slices, maps, arenas,
handles, channels, scheduler, tasks/cancellation, futex-based synchronization,
files, time, randomness, networking, panic diagnostics, cleanup, and resource
census. C headers may remain compatibility/oracle code but not required runtime
behavior for the independent milestone.

## Phase 7 — bootstrap and product driver

Implement `mako bootstrap` and the required CLI commands in Mako. Build stage 1
from stage 0, rebuild stage 2 from stage 1, rebuild stage 3 from stage 2, and
run the same corpus at every stage. Capture normalized hashes, dependency
traces, diagnostics, and clean-machine results as release artifacts.

## Phase 8 — hardening and declaration

Run memory, concurrency, negative-diagnostic, reproducibility, compatibility,
clean-machine, and performance gates. Audit the stage-1+ dependency graph for
forbidden tools and dynamic loading. Only then update status documents to state
that the fixed point is complete.

## Capability matrix

This matrix is intentionally a template until Phase 0 inventory work is done.
Rows should be split into smaller capabilities as implementation begins.

| Area | Capability | State | Stage 0 | Mako compiler | Differential gate | Bootstrap gate |
|---|---|---|---|---|---|---|
| Frontend | Lexer/parser/source spans | partial | yes | partial | pending | pending |
| Frontend | Packages, resolution, types, generics | partial | yes | partial | pending | pending |
| Safety | Ownership, NLL, escape, drops | partial | yes | partial | pending | pending |
| Middle end | Verified typed CFG/SSA IR | partial | yes | partial | pending | pending |
| Backend | Direct x86-64 machine encoding | partial | Rust backend | yes: scalar/call/branch/loop subset, slot-per-value frames up to 127 slots, SysV stack-passed arguments, multi-carried-local while loops with break/continue guards, calls, nested if/else merges, body temporaries, sequential loops with guarded tails, a leading merge-if, and string literals with `print`/`exit`/`len` including inline-itoa `print(int)` and plain-while-body prints | stage-0/stage-1 execution differential on 69 fixtures | pending |
| Binary | ELF64 writer and static link resolution | partial | external tools | yes: single-segment static executables, direct `call rel32` linking | SHA-256 image pins + Linux execution | pending |
| Runtime | Linux syscall-native mandatory runtime | partial | C/Rust support | `_start` bridge, `exit`, `mmap`/`munmap` byte-slice allocator, owned string objects, argv access, and file open/read/write/close | `selfhost-linux` CI job executes every generated image | pending |
| Language | Fixed-width types, bytes, layout, unsafe | partial | mixed | partial: `[]byte` and `[]int` complete incl. clone/append | pending | pending |
| Language | Atomics, function pointers, const evaluation | unsupported | mixed | no | pending | pending |
| Tooling | Mako-native bootstrap and product commands | partial | Rust driver | no | pending | pending |

## Known stage-0 defects found by this work

- **Use-after-move on slices is accepted and segfaults.** Reading a slice
  binding after passing it to `append` passes `mako check` and then crashes at
  run time (10/10 on a minimal case; non-deterministic in larger ones). It
  should be an ownership diagnostic. Stage 0 is the oracle for every
  differential gate here, so fixtures must avoid the pattern until it is fixed.
  The self-hosted IR clones on transfer and is unaffected.

## Measured distance to a Mako-only compiler

Measured 2026-08-03 by running stage 1 over each `compiler/*.mko` source and
over single-feature probes. This is the worklist; it replaces guesswork about
what "almost self-hosting" means.

`compiler/*.mko` declares **231 functions**. Stage 1 lowers **24** of them
(~10%), up from 19 before the general-path work. Every unsupported input is
rejected, not miscompiled.

Nested `if`/`else` at any depth, scalar `let` bindings, global `const`
references, and calls are no longer blockers — they lower through the general
path below.

**Only 71 of the 243 functions have scalar-only signatures.** Allowing `[]int`
as well raises that to **130** (59 functions use `[]int`); the remaining 113
take or return a struct or a map. So scalar control flow alone tops out at 29%,
and scalar plus `[]int` at 54% — which is why three consecutive control-flow
increments (`let`, calls) moved coverage by 0, and only the typer fix for global
consts moved it at all (+3).

### Which single feature unlocks the most

Each function was classified by which of four capabilities its signature and
body require: loops, struct field access, struct literals, and owned values
(`string`/`[]T` parameters or returns). Counting only functions blocked by
**exactly one** of them — i.e. what a single feature would unlock outright:

| blocked by exactly one feature | functions |
|---|---|
| only owned values | **50** |
| only struct literals | 24 |
| only struct field access | 1 |
| only loops | **0** |

Distribution overall: 14 functions need none of the four, 75 need exactly one,
37 need two, 24 need three, 20 need all four.

Two conclusions. **Owned values is the highest-value single feature** and loops
are the lowest — no function anywhere is blocked by loops alone, which is why
loop work should not be prioritized despite 76 functions containing a `while`.
And the tail is genuinely entangled: 81 functions need three or more, so the
last stretch cannot be unlocked one capability at a time.

Blocking capabilities, each confirmed by a probe that stage 1 refuses:

| Blocker | Stage-1 result | Used by the compiler |
|---|---|---|
| Owned (`string`, `[]T`) parameters in the general path | no ownership ops emitted | **146 of 246 take a slice param** — the dominant blocker |
| Struct literal + field access | no `SEM_STRUCT` in the type system at all | 35 struct declarations; needs typer + IR + backend |
| `[]T` for T a struct | typed IR cannot lower | pervasive: most arenas are `[]SomeStruct` |
| Guard clause before a loop (`if ... return` then `while`) | typed IR cannot lower | pervasive |
| Nested `while` | typed IR cannot lower | common |
| `match` | typed IR cannot lower | 9 sites |
| Struct-typed parameters | typed IR cannot lower | pervasive |
| String concatenation | `x86 scalar body operator is invalid` | 39 sites |
| `[]int`/`[]T` `make`/`append` (only `[]byte` exists) | `x86 byte make invalid` | pervasive |
| Multi-file packages | IR call targets a function without a body | required: 15 sources, 91 functions in `x86_64.mko` alone |

The architectural consequence matters more than the individual rows. Both the
IR lowerer and the backend are **shape matchers**: `lower_typed_ir` enumerates
specific statement patterns (one `if`, one `while` with N carried locals, a
sequential chain, one guard, one merge-`if`), and `x86_lower_scalar_body` and
its siblings match block-count shapes (three-block, four-block, seven/eight
block). Each new program shape costs a hand-written case, and arbitrary nesting
is a combinatorial explosion of shapes. 231 functions of ordinary Mako cannot
be reached this way.

Closing the gap therefore means the Phase 2/3/4 work as written, not more
shapes: general CFG construction from arbitrary statement trees, SSA with
dominance-frontier block parameters, struct layout and aggregate lowering,
general instruction selection, and a register allocator. Until then stage 1 is
a verified compiler for a narrow subset, which is a useful oracle-checked
foundation but is not a bootstrap.

### Migration order out of shape matching

Step 1 is done end to end. `ir_lower_returning_if_tree` builds a CFG by walking
the statement tree, and `x86_lower_returning_cfg` encodes any all-paths-return
branch tree at any nesting depth. Both are wired as last-resort fallbacks, so
every matched shape keeps its own path and its pinned images. Verified against
the stage-0 oracle on Linux x86-64 at four nesting levels and with nested
`else` arms in both branches.

This proves the general design — statement-tree walk, arena-order expression
lowering, post-layout displacement patching — but it does **not** yet move the
lowered-function count over `compiler/*.mko`, because nearly every function
there binds locals and calls other functions. Step 2 is what starts converting
real compiler code.

The remaining steps, in dependency order:

1. ~~General IR CFG construction for all-returning branch trees.~~ Done.
2. ~~Scalar `let` bindings inside the branch tree.~~ Done. It did **not** move
   the lowered-function count: measured after landing, coverage is still 19 of
   231, because almost every function in `compiler/*.mko` either calls another
   function or runs a loop. Recording this so the same assumption is not made
   twice — `let` support was necessary but is not sufficient for any real
   function here.
3. ~~Global `const` references.~~ Done, and the first change to move real
   coverage: **19 → 22** lowered functions. The fix was in the typer — a const
   reference stayed `SEM_UNKNOWN`, which made every expression built on it look
   unsupported. The shape-matched path still fails the whole compile on a const
   rather than skipping, because materializing one there must be routed into an
   arm bucket by the shape-specific routing; that resolves by migrating those
   shapes, not by patching routing.
4. ~~Calls inside the branch tree.~~ Done. Like `let`, it moved coverage by
   **zero** — still 22. That is the third time a control-flow/expression
   increment failed to reach real compiler code, and the measurement below
   explains why for good: **only 71 of 243 functions have scalar-only
   signatures**, so 172 are gated on aggregates no matter how general the
   control flow becomes. Scalar work has a hard ceiling of 29%.
5. ~~Ownership operations in the general path.~~ Done — drops, moves, and
   clone-on-pass, each verified on Linux by allocation balance as well as exit
   status. Coverage moved 22 → 24 across the group, well short of the 50 the
   "blocked only by owned values" count suggested: those functions also use
   loops or struct values, so the classification counted signature-level
   blockers, not the full body. The single
   feature by measurement: **50 functions are blocked by owned values and
   nothing else**, and 146 of 246 take a slice parameter. It is a coherent group
   of three, not one change:
   - `IR_DROP` for each owned parameter or local not consumed on a return path;
   - `IR_MOVE` for an owned value that is returned;
   - `IR_CLONE` when an ordinary owned local is passed to a call, so the
     original stays valid.

   All three must land together — emitting drops without moves breaks linearity
   for owned returns, and the ownership verifier rejects either half on its own.
   That verifier is the safety net worth leaning on here: it enforces
   exactly-once consumption per path, so a mistake fails the gate rather than
   producing a double free. One layer, no typer work.

   Relaxing parameters and locals from `int` to any non-owned scalar (`int`,
   `bool`, `byte`) was the cheap half of this and moved coverage 22 → 23. The
   restriction to `SEM_INT` had been my own conservatism, not a real constraint.
6. **Struct layout, field access, and `[]Struct`.** The next step, and the
   first that spans all three layers. There is no `SEM_STRUCT` kind at all
   today: the semantic types are scalars, arrays of scalars, map, and byte, and
   neither the typer nor the IR mentions `EXPR_FIELD`. So it needs a type kind
   plus a field table (name, type, offset per struct item), struct-literal and
   field-access typing, an IR representation, and backend addressing.

   **Representation: one slot holding a pointer, not N flattened slots.** The
   slot model gives each SSA value exactly one 64-bit slot, so a multi-slot
   struct value does not fit SSA at all — whereas a pointer to a field block
   does, and it is exactly how slices already work (`[length, capacity, data]`
   behind a header pointer, one slot). That choice means field access is a load
   at a constant offset, struct literals allocate and store each field, and
   `x86_one_slot_type` extends by one entry instead of the slot allocator being
   rewritten. It also makes `[]Struct` fall out: the element stride is the
   struct size, which `x86_slice_element_size` already parameterizes.

   The cost is an allocation per struct literal, which the existing slice
   emitters already pay via `mmap`, and it brings structs under the ownership
   machinery completed in step 5 — drops, moves and clone-on-pass all apply
   unchanged if struct types join `ir_type_is_owned`. Escape analysis to put
   non-escaping structs on the stack is a later optimization, not a prerequisite.

   Suggested first slice: a struct with scalar fields only, constructed by a
   literal and read by field access, within one function — no struct parameters
   or returns, which avoids multi-word SysV argument passing entirely.

   **Do the prerequisite first: annotated `let` bindings do not retain a type
   root.** An attempt at field typing failed on this, not on the field logic.
   `LocalBinding.type_root` is populated for *parameters* — the bring-up notes
   say so explicitly — but for `let p: P = ...` there is nothing to resolve `P`
   from, so struct identity cannot be recovered at a use site. Field access
   typing needs the `let` annotation parsed into the shared type arena and
   stored on the binding before any of it works. Verify with a probe that a
   struct-annotated local reports a non-negative `type_root` before writing
   field lookup.

   Two pieces that did work and can be rewritten quickly: resolving a
   `TYPE_NAMED` root to an `ITEM_STRUCT` by comparing the item's `name_start`/
   `name_end` source slice, and classifying a field's declared type straight
   from its body tokens (struct bodies are not in the type arena, so
   `semantic_declared_type` does not apply to them). `[]int` is done for reads
   (`make`/index/`len`, any element stride); `append` on `[]int` still needs
   `IR_CLONE` for slices in the backend, which is a small, well-scoped piece.
   Reaching **130** of 243 functions needs `[]int` complete; the remaining 113
   need struct-typed values.
7. **Merge blocks and block parameters** via dominance frontiers, which
   subsumes the hand-written merge-if and loop shapes. This is where arms stop
   being required to return, and where loops inside the general path become
   possible.
8. **General block encoding for jumps and backedges**, subsuming the loop,
   guarded-loop, chain, and pre-if encoders.
9. **Multi-file packages.** Required regardless: single-file compilation cannot
   resolve calls across the fifteen sources, so `x86_64.mko` and its 91
   functions cannot even be verified today.
10. *(folded into step 6)*
11. **Register allocation**, replacing the slot-per-value model.

Each step should delete shape-specific code rather than accumulate beside it;
the dispatch chain in `x86_encode_function` and the shape flags in
`lower_typed_ir` are the two lists that must shrink to nothing.

For every new row, record the specification, implementation owner/path,
positive and negative tests, safety classification, target assumptions,
reproducibility impact, and the exact command that proves the gate.

## Definition-of-done checklist

- [ ] No required stage-1+ dependency invokes or loads a forbidden tool.
- [ ] Stage 0 builds stage 1 from Mako sources.
- [ ] Stage 1 builds stage 2 from the same sources.
- [ ] Stage 2 builds stage 3 or the agreed corpus reproducibly.
- [ ] Stage behavior and diagnostics are equivalent within documented rules.
- [ ] Linux x86-64 static ELF programs build and run on a clean machine.
- [ ] Mandatory runtime behavior is no longer implemented in C.
- [ ] Reproducibility, memory-safety, compatibility, and performance reports
      are checked in with the release decision.
