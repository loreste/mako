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

State is `implemented` only when a gate proves it. `partial` means a named
subset works and everything outside it is refused, never miscompiled.

| Area | Capability | State | Mako compiler covers | Gate |
|---|---|---|---|---|
| Frontend | Lexer, parser, source spans | partial | whole compiler corpus sweeps in ~3 s | frontend pins |
| Frontend | Packages | partial | comma-separated path list concatenated into one translation unit; no module system, no visibility | package pin + unreadable-member negatives |
| Frontend | Resolution, types | partial | scalars, strings, slices of int/byte/string, maps, POD structs and their slices | arena-shape pins |
| Frontend | Generics, interfaces, patterns | unsupported | no | — |
| Safety | Ownership: moves, clones, drops | partial | owned strings and slices, clone-on-pass to declared callees, borrow-vs-consume for intrinsics, block-scoped bindings | `strace` mmap/munmap balance; 20k-iteration soak |
| Safety | Verified typed IR | partial | dominance, block parameters, exactly-once consumption per path | verifier runs on every compile |
| Middle end | Optimisation passes | unsupported | two peepholes only (redundant frame load, dead store) | 125-fixture execution differential |
| Backend | Direct x86-64 encoding | partial | scalars, calls, nested if/else with merges, `while` with block parameters, assignment, indexing on int/byte slices, owned strings (`format_int`, concat, clone), `str_byte_at`, struct slices and fields, argv, file I/O | SHA-256 image pins + Linux execution |
| Backend | Register allocation | partial | one working register; a value is kept in rax across a single instruction boundary. No live intervals, no register file, no spills | frame-traffic counts + differential |
| Binary | ELF64 writer, static linking | partial | single-segment static executables, direct `call rel32` | image pins + Linux execution |
| Runtime | Linux syscall-native runtime | partial | `_start`, `exit`, `mmap`/`munmap` allocator, owned string objects, argv, open/read/write/close | `selfhost-linux` executes every pinned image |
| Language | Fixed-width types, bytes, unsafe | partial | `[]byte` and `[]int` incl. clone/append/drop | pins |
| Language | POD structs | partial | `[]Struct` make/len/param, `slice[i].field`, element forwarding, struct params as borrowed elements. Struct literals, struct returns, non-POD structs, and **indexing a struct slice for its element** unsupported | pins |
| Language | Concurrency, atomics, function pointers, const eval | unsupported | no | — |
| Tooling | Mako-native bootstrap and product commands | unsupported | stage 1 is a single-purpose driver: source in, ELF out | — |

The two rows that decide the fixed point are **Tooling** and **Backend**: stage 1
lowers 54 of 297 of its own functions, so it cannot yet compile itself, and it
has no bootstrap driver.

## Known stage-0 defects found by this work

- **Use-after-move on slices is accepted and segfaults.** Reading a slice
  binding after passing it to `append` passes `mako check` and then crashes at
  run time (10/10 on a minimal case; non-deterministic in larger ones). It
  should be an ownership diagnostic. Stage 0 is the oracle for every
  differential gate here, so fixtures must avoid the pattern until it is fixed.
  The self-hosted IR clones on transfer and is unaffected.

- **Reading a field of a `make`d struct slice segfaults.** A program that does
  `let pts: []Pt = make([]Pt, 1, 4)` and reads `pts[0].x` exits 139 under stage
  0, and aborts on the out-of-range form. Stage 1 compiles both correctly:
  zero-initialised element, and exit 70 from its bounds trap. This removes the
  differential oracle for every struct-slice fixture until it is fixed.

- **The C backend cannot build the compiler package.** `--backend c` fails with
  ~20 "use of undeclared identifier" errors for `const`s defined in one file and
  used in another (`X86_OK`, `ELF_OK`, `IR_MOVE`, `IR_CALL`). It builds ordinary
  programs and same-file consts fine, so this is cross-file const emission only.
  The native backend, which is the default, is unaffected.

## Measured distance to a Mako-only compiler

Remeasured 2026-08-06 with stage 1 over its own sources. Stage 1 records why
each function declined and names it, so this is read off the compiler rather
than estimated.

Two numbers, because they mean different things:

| measurement | value |
|---|---|
| compiled **as one package** (the real number) | **54 of 297 signatures — 18%** |
| compiled **one file at a time** | 47 |

The gap between them is the module system: a type declared in one file is
invisible in the next, so every parameter of such a type types as `SEM_UNKNOWN`.
Package mode is what the compiler will eventually have to compile itself in.

Every unsupported input is rejected, not miscompiled — a property that has not
been free. Three fail-open miscompiles were found and closed while getting here,
each by running a program rather than reading a diff:

- `slice[i].field` ignored the field offset entirely, so `pts[0].x` and
  `pts[0].y` compiled to byte-identical images.
- A straight-line assignment emitted **nothing at all**: `let mut i = 3; i = 7;
  exit(i)` exited 3, and `xs[1] = 40; exit(xs[1])` exited 0.
- A store in one `if` arm stayed visible in its sibling, so a value was
  referenced from a block that does not dominate its definition.

### Current decline reasons, package mode

| count | reason |
|---|---|
| 54 | needs a known type for this **call** |
| 38 | calls a function that could not be lowered (cascade, not a gap) |
| 36 | cannot index a slice of structs |
| 28 | supports only scalar literal, unary, and binary expressions |
| 14 | needs a known type for this **identifier** |
| 14 | parameter type is not bindable |
| 12 | carries only non-owned scalars through a loop |
| 10 | needs a known type for this **array literal** |
| 9 | cannot merge statements after an if/else |

These counts exceed the function total because the callee cascade re-attempts a
banned function each round. They rank the work; they are not a headcount.

The cascade entry is worth reading separately: those 38 are functions with no
problem of their own, waiting on a callee. They were previously misfiled under
"found no statements in the body", because a banned function substituted the
wrong item and reported whatever *that* item's body said.

### The single biggest gap: non-POD struct values

The old "needs a known type" bucket was 78 declines and unanalysed. Naming the
expression kind in the diagnostic split it in one measurement — 54 calls, 14
identifiers, 10 array literals — and the calls and identifiers turn out to be
the same thing.

**Twenty-four percent of this compiler's functions return a struct the backend
cannot represent:**

| return type | functions | |
|---|---|---|
| `bool`, `int`, `[]int`, `string`, … | 228 | fine |
| `X86Code` | 22 | ineligible |
| `X86Body` | 16 | ineligible |
| `ElfImage`, `TypeParse`, `IrCfgLowering`, `ParseResult`, … | 33 | ineligible |
| **total returning an ineligible struct** | **71 of 299** | |

A struct is eligible only when every field is a one-slot scalar. `X86Code` is
`{ bytes: []int, status: int, error: string }` — an owned slice and an owned
string — so it is not, and every call to a function returning one types as
`SEM_UNKNOWN`, as does every identifier bound to the result.

This is a larger item than struct-slice indexing and it is the true top of the
list. It needs four things, and the third is where the danger is:

1. A semantic kind for a non-POD struct.
2. A representation — multi-slot value or a heap block with a pointer.
3. **Ownership.** The struct owns its `[]int` and its `string`, so a drop must
   free each owned field, and a partially-moved struct must not free what was
   moved out. This is exactly the problem the POD design note recorded as *not
   arising* — it arises here.
4. Returning one, which is either a multi-value return or an out-pointer.

Until it lands, the compiler's own idiom — a result struct carrying a payload,
a status and an error string — is unrepresentable, and no amount of work on
control flow or indexing will change that.

### What the parameter surface looks like now

Of the compiler's parameters, the only unbindable ones left are the genuinely
ineligible structs — `StructLayout`, `X86Code`, `X86Body`, `TypedIr` — which
have slice or string fields. Every heavily-used struct (`Token`,
`IrInstruction`, `IrFunction`, `IrBlock`, `ExprNode`, `StmtNode`, `Symbol`, …)
is eligible and binds. Struct eligibility is not the blocker it once looked
like; **struct-slice indexing** is, at 36 declines.

### Compile speed: fixed, and it was the binding constraint

Stage 1 needed **162 s and 5.8 GB** for its own largest source, and a Linux
x86-64 host with 8 GB OOM-killed it during the gate's frontend sweep — before a
single backend check ran, losing the only evidence a non-Linux developer host
cannot produce.

Profiling put the cost in `parse_expression`, whose every recursive return
carried the whole node arena inside `ExprParse` to be deep-cloned: one arena
copy per parsed node. A parse function must own the arena it appends to, and
Mako has no way to lend one — `mut` parameters do not propagate to the caller,
and there is no module-level mutable state.

Parsing each statement into a scratch arena and merging with a uniform index
shift bounds the copy by one statement's expression tree:

| source | before | after |
|---|---|---|
| `z_type.mko` | 1.53 s / 156 MB | 0.04 s / 13 MB |
| `zzzzz_types.mko` | 4.31 s / 411 MB | 0.09 s / 20 MB |
| `zzzzzz_ir.mko` | 162 s / 5837 MB | **1.60 s / 206 MB** |

Output is byte-identical, which every pinned image hash confirms. The whole
corpus now sweeps in about three seconds, and the **full gate passes on Linux
x86-64 with every pinned ELF executed and none deferred**.

The underlying language limitation remains: threading an arena by value is the
only option today. A borrowed parameter form would remove the class rather than
this instance, and is the strongest argument for a language change.

### A fail-open miscompile, found and closed

Typing `slice[i].field` was enough to make it *lower*, before any lowering for
it existed. `return pts[0].x` and `return pts[0].y` compiled to byte-identical
images — the field offset was ignored entirely.

The cause predated the struct work: the matched lowering path read
`if op > 0 { emit }` with no `else`, and `ir_expression_opcode` returns 0 for
any kind it does not model, so such an expression emitted **nothing** and
lowering continued as if the program had said something else.

It now fails closed: a function whose body holds an expression kind with no
opcode is declined before emission, unless the kind legitimately emits nothing
on its own (an identifier, a type reference). The check that catches it needs
no populated data — compile two field reads and compare the images — and is
worth reusing for any new addressed load.

### Whole-file poisoning, fixed

A lowered function calling a **skipped** one left a call site naming an item
with no IR body, and the verifier rejected the *entire package*: one
unsupported callee failed a whole file. `x86_64.mko` had been failing this way
throughout, which is why it contributed no statistics at all and earlier totals
silently excluded its 97 functions.

`lower_typed_ir` is now a fixpoint: lower, ban any function reaching a bodiless
callee, repeat until the banned set stops growing. Intrinsics are excluded.
No compiler source fails outright any more.


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
6. ~~Struct layout, field access, and `[]Struct`.~~ Done for POD structs, and
   the representation chosen differs from the one sketched here earlier. That
   sketch proposed one slot holding a pointer to an allocated field block, with
   struct literals allocating and structs joining `ir_type_is_owned`. Two
   measurements made a cheaper and safer design available:

   - Slices of structs outnumber struct values 137 to 38 in the compiler's own
     parameters, and `items[i].field` is one addressed load — so no struct
     value has to be materialised at all.
   - Every hot struct is POD, so a slice of one drops as a single allocation
     and no element needs its own drop.

   What was built instead:

   - A struct field table with a deliberately narrow eligibility rule: every
     field must be a single scalar token. A `string`, a `[]int` or a nested
     struct makes the struct ineligible, and an ineligible struct is invisible
     to lookup, so no later pass can bind one by accident.
   - `[]Struct` as one owned slot, encoded as
     `SEM_ARRAY_STRUCT_BASE + struct_index * 64 + field_count`. The identity
     half keeps two same-width structs distinct; the width half lets the
     encoder derive the stride from the kind alone, so nothing below the type
     checker is handed the struct table.
   - `x86_slice_element_size` returns `8 * field_count`, which is the single
     point governing make, index, append, clone and drop — the struct case
     reaches all five there rather than at five sites.
   - `slice[i].field` as one bounds-checked load, `data + i * stride + 8 * slot`.
   - Struct values as **borrowed element pointers**, not allocations: bindable,
     never owned, never dropped, nothing cloned per call. A local bound to an
     element forwards its `(slice, index)` pair, and a struct parameter receives
     an address materialised at the call site by `IR_ELEM_ADDR`.

   Guards, each checked against a program that must be refused: a `let mut`
   element local (it could be rebound after the read's operands were captured),
   an element used in any way other than a field read or a call argument, and a
   field a struct does not have. Bounds traps fire on both the fused read and
   the address computation.

   Still unsupported: struct literals, struct returns, non-POD structs, and any
   struct value not originating in a slice element.

7. ~~Root selection.~~ Done. The symptom was recorded as "a three-function
   chain fails", which was the wrong diagnosis — a three-function chain lowers
   fine. The actual rule was that the root had to be *the unique function no
   call site references*, so a single unused helper beside `main` gave two
   candidates and refused the whole image. Every real source has uncalled
   functions, so this rejected all of them.

   The root is now the function owning the item named `main`, resolved by
   `ir_entry_item` from the item arena and threaded through
   `elf64_lower_typed_ir` into `x86_lower_functions`. The uncalled rule remains
   as the fallback when no `main` exists, which is what the existing
   two-function fixtures use, and every pinned image is byte-identical because
   of it.

   Unreachable functions are still encoded into the image. That is dead code,
   not a correctness problem, and eliminating it belongs to Phase 3.

8. ~~String concatenation and `format_int`.~~ Done. Both allocate an owned
   `{length, bytes}` object and both are dropped.

   - `format_int` is intrinsic `-27`. It renders digits backward in the red
     zone first, because the length is not known until they are and the
     allocation has to be exact — the drop unmaps `length + 10`, so a padded
     allocation would leave the tail mapped. `INT_MIN` renders because the
     negation is read as unsigned, the same trick `print(int)` uses.
   - Concatenation is `IR_BINARY` with a `SEM_STRING` result. It consumes
     neither operand — it copies both payloads into a fresh allocation — so an
     operand that is a *temporary* gets an `IR_DROP` after the copy. That is
     what keeps `a + b + c` to one surviving allocation instead of one per
     link.
   - A literal operand counts as a temporary and carries a drop, but the
     backend's owned-producer check emits no `munmap` for it. A literal is a
     static view into the code image; the drop exists to close linearity, not
     to free. `print("literal")` already worked this way.

   Verified: images pinned, stage-0 oracle agreement on every rendered digit,
   execution on Linux x86-64, and `strace` allocation balance — 6/6 for
   `elf_format_int_exit`, 7/7 for `elf_concat_exit`.

9. ~~Block parameters and loops in the general path.~~ Done, both halves.

   `x86_lower_returning_cfg` used to refuse any block parameter and accept only
   `IR_RETURN` and `IR_BRANCH` terminators. It now accepts a leading run of
   block parameters and an `IR_JUMP` terminator, copying each jump argument into
   the target's parameter slot before transferring. In the slot model that copy
   *is* the phi — every value already owns a frame slot, so no register
   allocation is involved.

   Two guards keep it honest, because both failures are silent:

   - A block with parameters must be entered only by jumps. Nothing else writes
     a parameter slot, so a branch-reached parameter — or one on the entry
     block — would read whatever the frame held.
   - Jump arguments must not permute their target's parameter slots. The copies
     run in sequence, so a genuine swap needs a temporary this path does not
     allocate; it is refused rather than mis-encoded. A carried value passing
     through itself is the common case and is safe.

   **This was source-reachable immediately, through merges rather than loops.**
   Three fixtures that previously produced no image at all now compile, run, and
   are pinned: `typed_ir_if_else`, `typed_ir_merge`, and
   `typed_ir_cfg_owned_alias` (whose string clone balances 1 mmap / 1 munmap).
   Their arena shapes are unchanged — only the image is new.

   The lowering followed. `ir_lower_returning_if_tree` now handles `while` and
   assignment. A loop is three blocks — header, body, continuation — and the
   header's block parameters are **exactly the locals the body assigns**, which
   is read off the source rather than computed from dominance frontiers: a
   structured `while` gives the carried set directly. A local the body only
   reads keeps one definition that dominates the loop and needs no parameter.
   An assignment emits no instruction at all; it rebinds the local to the new
   SSA value, which is why `total = total + i` followed by `print(total)` reads
   the updated value — the "stale read" the matched shapes had to refuse.

   Four things this needed that were not on the sketch:

   - **If arms inside a body must inherit the backedge.** Falling off the end of
     an arm is falling off the end of the body. Without it the arm returned and
     the loop ran once — caught by a fixture exiting 0 instead of 10.
   - **The backedge drops what the iteration allocated**, measured from the
     loop's entry mark rather than the block's, since the block carrying the
     backedge may be an arm nested inside the body.
   - **The one-parameter jump encoding is mandatory**, not optional: the
     verifier rejects the wide `jump_arguments` form for a single-parameter
     target.
   - **Re-entering a definition through a backedge is not a double definition**
     when the previous value was already consumed. The ownership verifier
     rejected any redefinition; it now rejects only redefinition while the value
     is still live, which is the actual leak.

   A backedge that would permute its own parameters (`a = b` then `b = a + 1`)
   is declined: breaking that cycle needs a scratch copy and `IR_MOVE` is
   reserved for owned values. An ordinary counter loop never reaches it.

   **This delivered the memory bar's preferred evidence.** A per-call leak needs
   a loop under a leak checker, which nothing could run before. 20,000 and
   200,000 iterations of `format_int` in a loop each make exactly as many
   `munmap` calls as `mmap` calls, and peak RSS is 256 KB at both — flat in the
   iteration count. Without the backedge drop the larger run would map 800 MB.

10. ~~Cloning an owned string.~~ Done, and the distinction that made it risky is
    gone rather than preserved. `x86_emit_clone_string` **always allocates**,
    whatever the source was. A clone that tried to track whether its source was
    an mmap'd block or a static view would hand a later `IR_DROP` a value whose
    nature depends on a producer several instructions away, and getting that
    wrong unmaps read-only code. Allocating unconditionally removes the
    question; cloning a literal costs one page it did not strictly need.

    The harder half was the **callee** side. A string parameter's nature is not
    visible from the signature — one caller clones an allocation, another moves
    a literal — so a type-based drop is unsound where the same rule is fine for
    `[]int` (whose only producer is `make`). `x86_string_parameter_owned` proves
    it whole-program: the callee drops its string parameter only when *every*
    call site reaching that function fills that position with a value the
    owned-producer check accepts. One unprovable site, or no call site at all,
    and the parameter stays borrowed — a leak rather than a crash, which is the
    direction a missing proof has to fail.

    Two fixtures pull in opposite directions and both are pinned:
    `elf_string_clone_exit` (all three call sites clone, so the callee drops,
    5/5 under `strace`) and `typed_ir_cfg_hold_call_argument` (a `hold` binding
    moves a literal in, so the callee must not drop — its image is byte-for-byte
    what it was before this work).

11. ~~Multi-file packages.~~ Done. `read_package` takes a comma-separated path
    list and concatenates the members into one buffer, giving one span space and
    one symbol table without merging arenas. A path with no comma is one file and
    behaves exactly as before, so every pinned invocation is untouched. It fails
    closed per member: `read_file` returns "" for a missing path, a directory and
    a broken symlink alike, so a mistyped member would otherwise vanish into a
    package that still compiled. The compiler's own fourteen backend files now
    compile as one unit — 294 signatures, 43 lowered.

12. ~~The remaining intrinsics.~~ Done. `str_byte_at` was missing from the table
    and the typer; its contract is the runtime's, not the obvious one — **-1 out
    of range, not a trap** — and copying the bounds trap from the slice index
    emitter would have diverged from the oracle on every lexer scan.

    Naming the callee in the diagnostic then showed the other 39 declines were
    **all `len`**, which the shape encoders handle but the shape-independent
    path did not: it looked the name up in the intrinsic table rather than
    emitting the single header load `IR_LEN` already encodes. Reason 2 is now
    zero.

13. ~~Indexing in the general path.~~ Done for slices of int and byte: one
    bounds-checked load whose stride comes from the element type. A slice of
    structs is refused explicitly — its element is a borrowed pointer, not a
    loaded word, and admitting it would hand the encoder an index to load at the
    wrong width.

    This turned up **a miscompile that predated it**: a straight-line body falls
    through every shape matcher to the single-block path, which emits the
    statement intrinsics and the return and *nothing at all* for an assignment.
    `let mut i = 3; i = 7; exit(i)` exited 3, and `xs[1] = 40; exit(xs[1])`
    exited 0. Confirmed pre-existing by rebuilding without the indexing change
    and getting a byte-identical image.

    Such a body now declines and routes to the general path, which rebinds the
    local correctly; an indexed store is refused outright. Package-mode
    coverage went **58 → 42** as a result — sixteen functions that were being
    miscompiled now fail closed, which is the trade this codebase asks for.

14. ~~Assignment to an owned local.~~ Done. `out = append(out, x)` — the shape
    of every builder in this compiler — is a rebind, not a leak: the call
    consumes the old allocation and hands back the replacement, so dropping it
    again would be a double free, while `out = make(...)` hands it to nobody and
    needs the drop. Which happened is read off the instructions the right-hand
    side emitted, not guessed from the expression.

    Two things had to land with it. `make` had no lowering in the general path
    at all, so a typed slice `let` declined before any rebind was reached. And
    **bindings had to become block-scoped**: `owned_locals` was one array
    threaded through the whole function, so a store in one `if` arm stayed
    visible in its sibling and the sibling referenced a value defined in a block
    that does not dominate it. That surfaced as `IR left value does not dominate
    its use` on `x86_emit_int_rax`, whose `out = append(out, ...)` appears in
    three arms. Scalar assignment could have hit the same bug; owned rebinding
    is what made it reachable.

    Package-mode lowering 43 → 53.

15. ~~`[]byte` in the general path.~~ Done, and it was a leak as well as a gap:
    `SEM_ARRAY_BYTE` was missing from `ir_type_is_owned`, so no `IR_DROP` was
    ever emitted for one and every `make([]byte, n)` leaked its block. Fixing it
    exposed two more leaks it had been hiding — the matched path cloned owned
    arguments passed to *intrinsics*, which borrow rather than take ownership,
    and a borrowing intrinsic was modelled as consuming, so the value was never
    dropped and reusing it afterwards was wrongly rejected as a use-after-move.
    The borrow rule is confined to slices: applying it to strings made a
    borrowed string parameter demand a drop its callee must never emit.

16. **Register allocation.** Started, at the cheapest end. The emitter tracks
    which slot rax still holds and drops a load that reads back what rax already
    has — 13.7% of frame loads across the pinned images, proven behaviourally
    identical by compiling all 125 fixtures with and without the elision and
    executing both on Linux: 248 of 248 runs agree on exit status and stdout.

    It is deliberately done at the head of an instruction's emission rather than
    as a peephole over the finished byte stream. Displacements are patched after
    layout from recorded offsets, and emitters such as `x86_emit_index_byte`
    carry their own internal relative jumps around the bounds trap, so deleting
    bytes anywhere else would shift offsets and break those jumps.

    The store goes too when the value has no other reader anywhere in the
    function, so a single-use temporary never reaches the frame at all —
    produced in rax, consumed from rax. Together: **frame loads 870 → 753,
    stores 1013 → 938, total frame traffic down 10.2%**, again 248 of 248
    identical runs.

    Two rules keep the store removal sound. Only opcodes whose emitters are
    straight-line and end with the store qualify — one with an internal jump,
    like the bounds trap in an indexed load, can target the bytes after its
    store. And the *consumer* is excluded from the liveness scan, since its read
    is the one already satisfied out of rax; counting it was why the first
    version removed no stores at all.

    **Immediate folding is the next step and was attempted and backed out.**
    A literal whose only reader is the following ALU op should fold into an
    `add rax, imm32` rather than being materialised into a slot — `i + 1` and
    `i < n` are most of the arithmetic in a loop. It works, but it shifts the
    layout that `x86_64_smoke_test` pins as eighteen absolute byte offsets, and
    recomputing those by inference would either weaken the self-check or block a
    correct change on a wrong guess. It wants those assertions expressed
    structurally first.

    What remains after that is the real thing: a linear-scan allocator over the
    general CFG, so an operand that is not the immediately preceding result
    stops being a memory reference. Every value still lives in a frame slot,
    which is why this backend is far off LLVM and why nothing about the
    product's speed depends on it yet.

17. **Non-POD struct values.** The top item by measurement: 54 call declines
    plus 14 identifier declines, and **71 of 299 functions return one**. A
    struct is eligible today only when every field is a one-slot scalar, so
    `X86Code { bytes: []int, status: int, error: string }` is not, and every
    call to a function returning it types as `SEM_UNKNOWN`.

    Ownership is the hard part and the reason this is not a routine extension:
    the struct owns its slice and its string, so a drop must free each owned
    field, and a partially-moved struct must not free what was moved out. The
    POD design note recorded that problem as not arising — for POD it does not;
    here it does.

    ### The design, decided but not built

    Traced far enough to know the shape, so the next attempt starts from a
    decision rather than a blank page.

    **Representation: one slot holding a pointer to an N-word heap block**, the
    way a slice already works. The alternative — N consecutive frame slots — is
    ABI-correct but forces a hidden-pointer return convention on every function
    that returns a struct, and 71 of them do. One slot returns through the
    existing convention unchanged.

    **Construction reuses the call machinery rather than a new opcode.** An
    `IrInstruction` has only `left`/`right`, and a literal has N fields, so the
    operands need an arena. `IR_CALL` with a struct-new sentinel callee already
    has one: `argument_start`/`argument_count` carry the field values, and
    `type_kind` carries the struct kind, from which `struct_kind_fields` gives
    N. No new arena, no new field on `TypedIr`, and the verifier's
    consumed-exactly-once rule applies to the field values for free.

    Field initialisers may be written in any order, so each one maps to its
    declared slot through the layout's field-name spans; the literal's order is
    not the storage order.

    **Clone on reading an owned field.** This is what dissolves the partial-move
    problem: if `let bytes = code.bytes` clones, then `code` still owns
    everything it ever owned and its drop is unconditional — drop each owned
    field, then free the block. No per-field moved-out state to track, which is
    the state that would otherwise have to be right on every path. It costs a
    copy per owned field read, which is the same trade already made for
    clone-on-pass.

    **Field read is already implemented.** `x86_emit_field_at` is an offset load
    on a pointer, which is exactly this once the object is a heap struct.

    Build order, each with its own allocation-balance soak: POD struct literals
    first — same representation, no owned fields, so the drop is just the block
    and the whole path is provable without the ownership question — then owned
    fields on top, which only extends the drop.

    Not started. Five interlocking parts (kind, typing, construction, field
    read, drop) in the memory-safety-critical path, and the typer has no
    `EXPR_STRUCT` handling at all today, so struct literals are currently
    untyped rather than typed-and-refused.

18. **Struct-slice indexing.** 36 declines. `items[i]` as a *value* is refused
    because a struct-slice element is a borrowed pointer rather than a loaded
    word, and admitting it naively would hand the encoder an index to load at
    the wrong width — the fail-open that once made `pts[0].x` and `pts[0].y`
    compile identically. `append(out, src[i])` needs a stride-wide copy inside
    the allocator's grow path rather than a one-word store.

19. **General SSA with dominance-frontier block parameters.** The structured
    `while` in item 9 got its carried set from the source, which is why it
    needed no dominance frontiers. Generalising it retires the shape matchers —
    each one retired is a reduction in surface, not a new feature — and is the
    largest single item left on this list by a wide margin.


## Definition-of-done checklist

Nothing here is complete. Two items are partly true and are marked so rather
than left blank, because "not started" and "works for the corpus but not for the
compiler" are different distances.

- [ ] No required stage-1+ dependency invokes or loads a forbidden tool.
- [x] **Stage 0 builds stage 1 from Mako sources.** Every build in this work
      does it; `scripts/selfhost-gate.sh` does it first.
- [ ] **Stage 1 builds stage 2 from the same sources.** The binding item.
      Stage 1 lowers 54 of 297 of its own functions and emits an image only when
      a source has no declined function, so it currently compiles none of its
      own files.
- [ ] Stage 2 builds stage 3 or the agreed corpus reproducibly.
- [ ] Stage behavior and diagnostics are equivalent within documented rules.
- [~] **Linux x86-64 static ELF programs build and run on a clean machine.**
      True for the fixture corpus: ~50 images pinned by SHA-256, checked against
      the stage-0 oracle, executed by the `selfhost-linux` job. Not true for
      arbitrary programs — the accepted language subset is the matrix above.
- [ ] Mandatory runtime behavior is no longer implemented in C. Barely started:
      the allocator, strings, argv and file I/O have Mako-native forms in the
      emitted images, but the shipping runtime is still C.
- [ ] Reproducibility, memory-safety, compatibility, and performance reports
      are checked in with the release decision.

### The shortest path to the third box

1. **Non-POD struct values** (54 calls + 14 identifiers). 24% of this
   compiler's functions return one. Nothing else unblocks its own idiom.
2. **Struct-slice indexing** (36) — `items[i]` as a value, and
   `append(out, src[i])` as a stride-wide copy in the allocator path.
3. **Loop-carried owned values** (12) — a builder loop is the compiler's own
   idiom and currently declines.
4. **Array literals** (10) — `[]` and `[a, b]` in the general path.
5. A bootstrap driver in Mako, which does not exist in any form.

Items 2–4 are each roughly the size of one increment in this log. Item 1 is
several, and item 5 has not been scoped. The 38 cascade declines are not on this
list because they resolve as their callees land, not by their own work.
