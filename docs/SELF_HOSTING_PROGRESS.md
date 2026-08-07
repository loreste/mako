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
- `scripts/selfhost-gate.sh`: passes on Darwin/arm64 **and on Linux x86-64**
  (frontend pins, ELF image pins, negatives). On Linux it executes every pinned
  ELF image rather than deferring, with no source skipped — which the quadratic
  fix below is what made possible.

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

## Resolved: the quadratic frontend

This was the binding constraint. It is fixed, and the fix is in Mako.

### The cause, from a sampled profile

Sampling stage 1 mid-run put the heaviest frames in `mako_native_unpack_nest`,
`mako_native_struct_key_clone` and `mako_native_struct_slice_clone_ptr`, called
from `parse_expression` and `parse_atom`.

A parse function has to *own* the arena it appends to, and Mako has no way to
lend one: `mut` parameters do not propagate back to the caller (200,000 pushes
through a `mut []Node` parameter leave the caller's slice at length zero), and
there is no module-level mutable state. So the arena is threaded by value, and
every crossing copies it. Reduced measurements, 20,000 appends each:

| pattern | time | peak RSS |
|---|---|---|
| append in place, same function | 0.25 s | 9 MB |
| return a bare grown slice | 20.2 s | 4.8 GB |
| return it wrapped in a struct | 110 s | 4.7 GB |

`attach_statement_expressions` passed the whole growing arena into the parser
once per statement, and every nested call copied it again: one arena copy per
parsed node, quadratic in both time and memory.

### The fix

Each statement now parses into a fresh scratch arena and the result is merged
into the shared arena with a uniform index shift. The copy is then bounded by
one statement's expression tree instead of by the whole file.

The merge is safe because nodes arrive in exactly the order they would have
been appended in, so every index moves by the same base. Of the twenty-two
`ExprNode` constructions in the parser, `left` is an expression index in all
but one: `EXPR_TYPE_REF` stores a *token* index there and must not be shifted.
`right` is an expression index or -1 everywhere. (`EXPR_MAKE.left` looks
ambiguous but is always an expression index — `type_root` is the index of the
`EXPR_TYPE_REF` node in the expression arena.)

Because the shift is uniform, the resulting arena is identical. Every pinned
expression count and ELF image hash in the gate is unchanged, which is what
proves this was a pure performance change.

### Result (Darwin/arm64)

| source | size | before | after |
|---|---|---|---|
| `z_type.mko` | 27 KB | 1.53 s / 156 MB | 0.04 s / 13 MB |
| `zzzzz_types.mko` | 63 KB | 4.31 s / 411 MB | 0.09 s / 20 MB |
| `zzzzzz_ir.mko` | 370 KB | 162 s / 5837 MB | **1.40 s / 188 MB** |
| `x86_64.mko` | 285 KB | not reachable | 1.35 s / 186 MB |

113x faster and 31x less memory on the largest source. The whole compiler
corpus now sweeps in 3.1 s.

The consequence that mattered: the gate previously OOM-killed `makoc-stage1`
(7.8 GB anon RSS) on a Linux x86-64 host with 8 GB of RAM, dying in the
frontend sweep before a single backend check ran — losing the only evidence a
non-Linux developer host cannot produce, since ELF execution is deferred there.
**The full gate now passes on Linux x86-64 with no source skipped and every
pinned ELF executed.**

`MAKO_SELFHOST_MAX_SOURCE_KB` remains as a safety valve for smaller hosts. It
defaults to off and prints the name of every source it skips, so reduced
coverage is stated rather than silent.


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

Both now run on Linux x86-64 under the gate, so the enabled `munmap` is known
not to double-free or crash. Neither has the loop-under-a-leak-checker test the
memory-safety bar asks for: that needs a loop calling the primitive thousands
of times, and loops in this path are one of the blockers listed below. Correct
execution is evidence against a double free, not evidence of leak-freedom.

## Remaining blockers, measured rather than guessed

`lower_typed_ir` used to discard the reason a function declined, so the work
list was guesswork. It now records each reason in `TypedIr.skip_reason`, and
the driver prints them when given a fourth argument. The gate never passes one,
so pinned output is unchanged.

Across the whole compiler corpus stage 1 lowers 27 of 155 functions (17%).
The 128 declines rank as:

| count | reason |
|---|---|
| 61 | CFG lowering parameter type is not bindable |
| 57 | CFG lowering needs a known type for every expression |
| 4 | CFG lowering supports only if, let, print, exit, and return statements |
| 3 | CFG lowering string literal has no owner on this path |
| 2 | CFG lowering supports only calls to declared functions and intrinsics |
| 1 | CFG lowering supports only print and exit statements |

The top two are 92% of the declines, and both come from the same gap. A
function taking a user-defined struct, or a slice of one, produces exactly
those two errors:

```mko
struct Rec { ok: bool  n: int }
fn check(r: Rec) -> int { ... }     // parameter type is not bindable
                                    // needs a known type for every expression
```

The semantic type system has no kind for a user-defined struct. `SEM_*` covers
int, float, bool, string, byte, arrays of those, and map — nothing else — so
every struct value and every field access types as `SEM_UNKNOWN`, and
`ir_cfg_bindable_type` rejects the parameter before ownership is even
considered. The compiler's own signatures are almost entirely `[]Token`,
`[]ExprNode`, `[]StmtNode` and `TypedIr`, which is why the skip rate is what it
is.

So the next milestone is user-defined struct types, end to end:

1. A semantic kind for a struct and for a slice of structs, carrying the struct
   index so field lookup is possible.
2. Field access typing, so `r.ok` is bool rather than unknown.
3. An IR and backend representation: layout, field load and store, and how a
   struct is passed and returned.
4. Ownership for a struct with owned fields — which fields are dropped, and
   when, given that a struct can be partially moved.

Item 4 is where the memory-safety risk sits and should be designed before item
3 is written.

Lower priority, and previously mistaken for the blockers: string concatenation
(no backend emitter for `format_int` either), `while` inside the general
if-tree, and cloning an owned string. That last one still has the hazard noted
below — an owned string is an mmap'd block while a literal is a static view in
the code image, so a clone has to preserve which it is or a later drop unmaps
read-only code.


## Struct types: foundation landed

The measured blocker above is user-defined structs. Two facts decided the
design before any code was written.

**Slices of structs dominate struct values.** Across the compiler's 511
parameters: 223 scalar, **137 slice-of-struct**, 113 scalar slice, 38 struct
value. The top slice types are `[]IrInstruction` (27), `[]Token` (23),
`[]IrConstant` (21), `[]IrFunction` (16), `[]IrCallSite` (15), `[]IrBlock`
(15). So the first slice targets `[]Struct`, not struct values, and
`items[i].field` can be a single addressed load — no struct value ever has to
be materialized, and no multi-value return is needed.

**Every hot struct is POD.** All thirteen — `IrInstruction`, `Token`,
`IrConstant`, `IrFunction`, `IrCallSite`, `IrBlock`, `TypeNode`, `StmtNode`,
`ExprNode`, `TopItem`, `ParamBinding`, `LocalBinding`, `Symbol` — have only
scalar fields. A slice of a POD struct is a plain heap block with a fixed
stride: it drops exactly like `[]int`, no element needs its own drop, and the
partial-move problem never arises. The ownership risk that made this milestone
look dangerous does not exist for the case that matters.

Landed, with a mutation-tested smoke test (`struct_layout_smoke_test`, wired
into the driver's self-check):

- `StructLayout`: per struct, its name span, field spans, field types, field
  count, and an eligibility flag.
- `build_struct_layout`, `struct_layout_lookup`, `struct_field_slot`,
  `struct_field_type`.
- `SEM_STRUCT_BASE` / `SEM_ARRAY_STRUCT_BASE`, so one `[]int` of semantic kinds
  can still name which struct is meant.

Eligibility is deliberately narrow: a field must be an identifier naming a
one-slot scalar. A `string`, a `[]int`, or a nested struct field makes the
whole struct ineligible, and an ineligible struct is invisible to
`struct_layout_lookup`, so no later pass can bind one by accident. The smoke
test asserts both directions, and inverting the builder's eligibility makes it
fail, so it is a real check rather than a passing assertion.

### Step 1 landed: struct slice typing

`semantic_declared_type` now resolves a slice of an eligible struct through the
struct table, so `[]Token`, `[]IrInstruction` and the rest carry a real
semantic kind instead of `SEM_UNKNOWN`. The kind is one integer holding both
halves:

    kind = SEM_ARRAY_STRUCT_BASE + struct_index * SEM_STRUCT_WIDTH_UNIT + field_count

The identity half keeps two structs of equal width from being interchangeable;
the width half lets the backend derive the element stride from the kind alone,
so nothing below the type checker is handed the struct table.
`struct_kind_distinct_smoke_test` asserts both halves round-trip and that two
same-width structs get different kinds.

Measured on the same file set, before and after, this halved both dominant
skip reasons: 61 -> 30 "parameter type is not bindable" and 57 -> 30 "needs a
known type for every expression".

Typing a slice as a real kind also broke `len` and `cap`, which recognised only
builtin slices and started rejecting the compiler's own sources. Fixed with
`semantic_is_slice`. Element extraction deliberately stays narrow: the element
of a struct slice is a struct value, which is still unmodelled, so
`semantic_array_element` keeps returning unknown for one.

### Fixed on the way: one unsupported callee poisoned a whole file

Lowering more functions exposed a pre-existing bug. When a lowered function
called one that had been skipped, its call site named an item with no IR body
and the verifier rejected the **entire package**, so a single unsupported
callee failed a whole file rather than one function. `x86_64.mko` had been
failing this way the whole time, which is why it contributed no statistics at
all.

`lower_typed_ir` is now a fixpoint: lower everything, ban any function whose
call reaches a callee without a body, and repeat until the banned set stops
growing. Intrinsics are excluded, since they are emitted inline and never have
a body. The loop is bounded by the signature count.

With that, no compiler source fails outright any more, and `x86_64.mko` lowers
16 of its 97 functions. Total lowered across the corpus went from 27 to 47.
Note the denominator changed at the same time: the old total of 155 functions
excluded `x86_64.mko` entirely because it could not be compiled.

### Step 2 landed: ownership and stride, together

These had to land in the same change. Marking the kind owned without a working
drop emitter leaks one slice per call, and `x86_slice_element_size` returned
zero for an unknown slice kind, which would have made the drop emit nothing at
all.

- `ir_type_is_owned` accepts a struct slice: every element is scalar, so it
  drops as one allocation and no element needs a drop of its own. Owned for the
  same reason `[]int` is.
- `x86_slice_element_size` returns `8 * field_count`, read straight off the
  kind. This one function governs make, index, append, clone and drop, so the
  struct case reaches all five here rather than at five separate sites.
- `x86_one_slot_type` accepts a struct slice, since it is a pointer.
- `semantic_type_reference` resolves `make([]Struct, ...)`.
- `x86_emit_make_byte` and `x86_emit_clone_slice` restricted the stride to 1 or
  8 and returned unchanged otherwise, which the callers correctly treated as a
  hard error rather than emitting a half-initialised slot. Both now accept a
  byte stride or any whole number of slots; the arithmetic was already
  stride-generic.

Proven end to end: a program that makes a `[]Pt`, passes it to a function
taking `[]Pt`, and takes its length compiles through the self-hosted backend
and exits 7 on Linux x86-64, matching the stage-0 oracle. The full gate passes
on Linux with every pinned ELF executed and none deferred, which is the
evidence that the clone-and-drop of a struct slice neither leaks into a crash
nor double-frees.

Aggregate lowered functions moved 47 -> 44. The capability is real and proven,
so this is ban interaction in the cascade rather than lost capability, but it
is not isolated further than that and should not be read as progress.

`len` on a struct slice works, which was step 4.

### Step 3 attempt found a fail-open miscompile

Typing `slice[i].field` was enough to make a field read *lower*, which it
should not have been, because no lowering for it exists. The check that caught
it needed no populated data: compile `return pts[0].x` and `return pts[0].y`
and compare the images.

    x-image: ff2ab7f4ea72cfb1...
    y-image: ff2ab7f4ea72cfb1...   byte-identical

The field offset was being ignored entirely, so both named the same slot. The
cause is not the struct work. In the matched lowering path the emitter reads

    let op = ...ir_expression_opcode(expression.kind)...
    if op > 0 { ...emit... }

with no `else`. `ir_expression_opcode` returns 0 for any kind it does not
model, so such an expression silently emitted **nothing** and lowering
continued as if the program had said something else. Anything typed but
unmodelled would take this path; a field read is just the first construct to
reach it.

Fixed by rejecting before emission rather than after: a function whose body
contains an expression kind with no opcode is declined, unless the kind is one
that legitimately emits nothing on its own — an identifier, which aliases a
binding, or a type reference, which belongs to the `make` around it. Declined
functions fall to the general path, which reports a reason instead of guessing.

The field-read typing rule was reverted at the same time, so `slice[i].field`
is refused again rather than miscompiled. Struct slice parameters, `make` and
`len` are unaffected and still lower.

Total lowered went 44 -> 45 with the stricter check, because a declined
function reaches the general path and some of those lower there.

### Step 3 landed: fused `slice[i].field`

A field read of an element of a struct slice is one bounds-checked addressed
load: `data + index * stride + 8 * slot`. `x86_emit_index_byte` could not
serve, since it scales only by 1 or 8 and adds no field offset — which is
exactly how the miscompile above read field zero whatever field was named.

- `IR_FIELD` carries the slice in `left`, the index in `right`, and packs the
  element width in slots with the field's slot into `aux`
  (`field_count * IR_FIELD_AUX_UNIT + field_slot`), so the encoder needs no
  struct table.
- The index expression underneath emits nothing; it is fused into the read. The
  pre-scan proves every struct-slice index has a field read to consume it, and
  declines the function otherwise, so a bare struct-slice index can never reach
  the encoder as a load of the wrong width.
- The pre-scan also declines any field read that is not this shape, or that
  names a field the struct does not have.

Verified three ways, since the usual oracle is unavailable here:

1. `pts[0].x` and `pts[0].y` now produce **different** images — the check that
   caught the miscompile.
2. The `.y` image is exactly six bytes larger and contains `48 05 08 00 00 00`
   (`add rax, 8`, slot one); the `.x` image contains no such instruction.
3. On Linux x86-64 both read zero-initialised elements and exit 3, and an
   out-of-range `pts[5].x` exits 70 from the bounds trap.

The stage-0 oracle cannot be used for this construct: it **segfaults** (exit
139) on reading a field of a `make`d struct slice, and aborts on the
out-of-range case. Stage 1 handles both correctly. That is a stage-0 defect,
not a stage-1 one, but it means struct-slice fixtures have no differential
oracle until it is fixed.

### What this does and does not unlock

Indexed field reads are 961 of roughly 5,450 field accesses in the compiler's
own sources. The other ~4,500 are reads of a struct *value*, almost always the
pattern

```mko
let node = types[root]
... node.kind ...
```

so the next step is struct values. The cheap form is to avoid materialising one
at all: a local bound directly to a struct-slice element can keep the (slice,
index) pair, and each `node.field` becomes the same fused read that already
works. That covers the dominant pattern without multi-slot values or
multi-value returns.

### Struct-value forwarding landed

The dominant pattern in the compiler's own sources is not the direct indexed
read but

```mko
let node = types[root]
... node.kind ...
```

It now lowers, without materialising a struct value. The `(slice, index)` pair
is forwarded through the local, so each `node.field` becomes the same fused
load as the direct form.

The missing piece was a *name*. Five expressions exist in that snippet and only
three were typed: `pts[0]` is untyped on purpose, but so was the identifier
`node`, and lowering declined on that. Bypassing the use-check changed nothing,
which ruled out the scan being too strict — the local simply had no type,
because a struct value had no semantic kind.

`SEM_STRUCT_VALUE_BASE` supplies one. It is deliberately a name and nothing
more:

- `ir_cfg_bindable_type` rejects it, so it can never occupy a slot, be passed,
  or be returned.
- `ir_type_is_owned` rejects it, since it is a view into a slice that owns the
  storage — dropping it would free another value's memory.
- Its range sits above every possible slice kind, so `semantic_is_struct_array`
  and `semantic_is_struct_value` can never both be true. The smoke test asserts
  that, and that the value kind round-trips to its slice kind and back.

Guards, each checked against a program that should be refused:

| case | result |
|---|---|
| `node.x` vs `node.y` | different images; only `.y` carries `add rax, 8` |
| `let mut node = pts[0]` | refused — a rebind after the operands were captured would answer for the wrong element |
| `take(node)` — value escapes | refused |
| execution on Linux x86-64 | both exit 9, matching zero-initialised data |

### Struct parameters by value: callee side landed

The ownership question this looked like it needed never arises. Every eligible
struct is POD, so a struct-value parameter is one **borrowed element pointer**:
the caller owns the storage, every field is scalar, and the callee can read
fields but can never free or copy anything. No clone per call, no escape
analysis.

- `semantic_declared_type` resolves a parameter declared as an eligible struct
  to that element's value kind.
- `ir_cfg_bindable_type` accepts a struct value — one slot holding the pointer
  — while `ir_type_is_owned` still rejects it, so it is never dropped. Dropping
  one would free the slice's storage out from under the owner.
- A field read whose object is already a borrowed element is a plain offset
  load, `IR_FIELD` with no index. `x86_emit_field_at` emits it: no length to
  check and no index to scale, because the pointer names the element.
- The field typing rule is now driven by the object's *type* rather than by how
  the object was formed, so it covers a parameter and a forwarded local with
  one rule.

`fn take(p: Pt) -> int { return p.y }` lowers. The full gate passes on Linux
x86-64 with every pinned ELF executed and none deferred, and the earlier
capabilities are unchanged: forwarding still lowers, the direct indexed read
still honours its offset, and a struct value that tries to escape into a call
is still refused.

### Caller side landed: struct parameters work end to end

Passing a slice element to a struct parameter materialises the element's
address at the call site. `IR_ELEM_ADDR` carries the slice in `left`, the index
in `right`, and the stride in `aux`; `x86_emit_element_addr` is the same
bounds-checked arithmetic as the fused field read without the final load.

The address goes into the existing `call_prefix`, alongside the clone that an
owned argument already emits — but nothing is cloned here. The slice keeps
ownership, the callee receives a borrow, and no drop is emitted for it.

The pre-scan was relaxed by exactly one case: a reference to an element local
may now be a call argument as well as a field object. Any other use still
declines the function.

Verified:

| check | result |
|---|---|
| `p.x` vs `p.y` through the parameter | different images — the offset survives the hop |
| execution on Linux x86-64 | both exit 6, matching zero-initialised data |
| `pts[9]` out of range | exits 70 from the bounds trap in the address computation |
| full gate on Linux, every ELF executed | passes |

So this compiles and runs:

```mko
struct Pt { x: int  y: int }
fn take(p: Pt) -> int { return p.y }
fn main() {
    let pts: []Pt = make([]Pt, 1, 2)
    let node = pts[0]
    exit(take(node) + 6)
}
```

The parameter surface is closed: of the compiler's 511 parameters, none is now
unbindable for want of a type. What remains are the other decline reasons, not
the shape of a signature.

### Root selection landed, and the recorded symptom was the wrong diagnosis

The last increment recorded "a three-function chain in a single file reports
`x86 image requires one uncalled root function`". Reproducing it showed a
three-function chain lowers and emits an image without complaint. The real
condition is narrower to state and far wider in effect:

```mko
fn spare(value: int) -> int { return value + 9 }   // nothing calls this
fn leaf(value: int) -> int  { return value + 1 }
fn main() { exit(leaf(3)) }
```

`x86_lower_functions` selected the root as *the unique function no call site
references*. Here that is two functions, so the image was refused. Every real
source declares helpers nothing calls, so this rejected all of them — the
earlier chain report was one instance of it, and the chain length was
incidental.

The root is now the function owning the item named `main`:

- `ir_entry_item(source, items)` returns the item index of `fn main`, or -1.
  It checks the item kind, so a struct named `main` is not an entry point.
- `elf64_lower_typed_ir` takes the entry item and threads it into
  `x86_lower_functions`, which uses it directly when it is non-negative.
- When it is -1 the uncalled rule still applies. That is not dead code: the
  existing two-function fixtures (`elf_call_add_exit`, `typed_ir_call`,
  `typed_ir_cfg_call`) declare no `main`, and keeping the fallback is why every
  pinned image in the gate is byte-identical after the change.

Evidence:

| check | result |
|---|---|
| `elf_uncalled_helper_exit` image | pinned by arena shape and SHA-256, `ir_skipped 0` |
| stage-0 oracle | exits 4 |
| the image on Linux x86-64 | exits 4 — rooted at `main`, not at `spare`, which would exit 10 |
| every previously pinned image | unchanged, so no existing root selection moved |
| `ir_entry_item_smoke_test` | in the driver's startup self-check; asserts the second of two functions is chosen and that a struct named `main` yields -1 |

Unreachable functions are still encoded into the image. That is dead code in a
single-segment static executable, not a correctness problem; eliminating it is
Phase 3 work.

This does not move the corpus count. No `compiler/*.mko` source reaches image
emission yet, because that still requires `ir_skipped 0` and every source has
declined functions. It removes a blocker that would have applied to all of them.

Struct *values* (38 parameters) stay unsupported, and so do non-POD structs.

## Owned strings: `format_int`, concatenation, and clone

Roadmap steps 8 and 10, landed together because they share one allocator and
one drop.

A string object is an mmap'd block: an eight-byte length header, the payload, a
NUL and a newline — `length + 10` mapped, and the drop unmaps exactly that.

### What lowers now

- **`format_int`** — intrinsic `-27`. Digits are rendered backward in the red
  zone first, because the length is not known until they are and the allocation
  has to be exact; a padded allocation would leave the tail mapped after a drop
  that unmaps `length + 10`. `INT_MIN` renders because the negation is read as
  unsigned, the same trick `print(int)` already uses.
- **Concatenation** — `IR_BINARY` with a `SEM_STRING` result. It consumes
  neither operand, so an operand that is a *temporary* gets an `IR_DROP` after
  the copy. Without that, every intermediate of `a + b + c` leaks.
- **Clone** — `x86_emit_clone_string` always allocates.

### Three things that were not obvious

**A literal's drop must emit nothing.** A literal is a static view into the code
image. It carries an `IR_DROP` so the ownership verifier sees it consumed, but
the backend's owned-producer check refuses to unmap it. Removing the verifier's
interest in string constants instead looked cleaner and was wrong: it broke the
use-after-move negative (`hold let first = "mako"; let second = first; return
first`), which needs exactly that tracking. The drop-with-no-unmap split is what
`print("literal")` already did.

**A string parameter cannot be dropped on type alone.** `[]int` can: `make` is
its only producer, so every `[]int` is an allocation. A string is not — one
caller clones an allocation, another moves a literal — and a type-based drop
unmaps the code image for the second. `x86_string_parameter_owned` proves it
whole-program instead: the callee drops only when *every* call site reaching
that function fills that position with a value the owned-producer check accepts.
No call site, or one that cannot be proven, and the parameter stays borrowed.
That leaks rather than crashes, which is the direction a missing proof has to
fail.

The check is load-bearing in both directions, and two pinned fixtures prove it:

| fixture | call sites | callee | image |
|---|---|---|---|
| `elf_string_clone_exit` | all three clone | drops the parameter | 1118 bytes, 5/5 allocations |
| `typed_ir_cfg_hold_call_argument` | `hold` moves a literal in | must not drop | 258 bytes — unchanged from before this work |

**Counting allocation sites in the image does not prove balance.** A single
callee drop site serves every call, and a literal's drop is deliberately no
bytes at all, so static counts disagree with reality for any multi-function
fixture. The gate counts `mmap`/`munmap` at run time under `strace` instead. The
first version of that check also miscounted: `mov eax, 9` matches an integer
constant of 9 as well as an mmap prologue, so it now requires the syscall
argument that follows.

### Evidence

Every image is pinned by arena shape and SHA-256, checked against the stage-0
oracle for stdout and exit status, executed on Linux x86-64, and balanced:

| fixture | covers | mmap/munmap |
|---|---|---|
| `elf_format_int_exit` | 42, -7, 0, 10^12, `INT_MIN`, `INT_MAX` | 6 / 6 |
| `elf_concat_exit` | literal+literal, owned+literal, empty either side, nested | 7 / 7 |
| `elf_string_clone_exit` | owned binding reused after passing, literal argument, concat temporary argument | 5 / 5 |

The literal-argument case is the one that matters most: without the
whole-program parameter check it would have unmapped read-only code.

### Not covered

The memory bar asks for a per-call leak to be caught by a loop under a leak
checker. No loop in this path lowers yet (roadmap item 9), so these are checked
by whole-run balance instead. That is real dynamic evidence, but it is one
iteration, not thousands.

A Mako function that *returns* a string is still never dropped by its caller —
the owned-producer check does not recognise a non-intrinsic call result. That
leaks rather than crashes, and it is the next piece of this area.

## Block parameters in the general encoder

Roadmap item 9, encoder half. `x86_lower_returning_cfg` refused every block
parameter and accepted only `IR_RETURN` and `IR_BRANCH` terminators. It now
takes a leading run of block parameters and an `IR_JUMP`, copying each argument
into the target's parameter slot before transferring. In the slot model that
copy *is* the phi — every value already owns a frame slot, so no register
allocation is involved.

Two guards, both for failures that are otherwise silent:

- **A parameterised block must be entered only by jumps.** Nothing else writes a
  parameter slot, so a branch-reached parameter would read whatever the frame
  held. The entry block is rejected for the same reason — the call fills
  `IR_PARAM`, not `IR_BLOCK_PARAM`.
- **Jump arguments must not permute their target's slots.** The copies run in
  sequence, so a swap would have the second copy read a slot the first
  overwrote. A permutation needs a temporary this path does not allocate, so it
  is refused. A carried value passing through itself is the common case and is
  left alone.

### It paid off through merges, not loops

The expectation was that this only mattered for `while`. In fact three fixtures
that had **never produced an image** compile immediately, because an if/else
merge is a parameterised join too:

| fixture | image | executed on Linux |
|---|---|---|
| `typed_ir_if_else` | 218 bytes | exit 1 / 2 / 4 for argc 1 / 2 / 4 |
| `typed_ir_merge` | 220 bytes | exit 1 with and without arguments |
| `typed_ir_cfg_owned_alias` | 426 bytes | exit 0, 1 mmap / 1 munmap |

Their arena shapes are byte-for-byte what they were; only the `elf_bytes` line
is new. All three are now pinned by hash and executed by the gate.

Three mutations confirm those checks bite rather than merely pass:

| mutation | caught |
|---|---|
| disable the permutation guard | yes |
| skip the jump argument copies | yes |
| start the segment before the block parameters | yes |

The second is the one that mattered: an earlier version of the test asserted
only the status and the presence of a backward jump, and a compiler that emitted
no argument copies at all — a loop that never advances — passed it. The test now
searches for the exact load/store pair.

## `while` and assignment in the general lowering

The other half of item 9. A loop is three blocks — header, body, continuation.
The header's block parameters are **exactly the locals the body assigns**, read
off the source rather than computed from dominance frontiers: a structured
`while` gives the carried set directly. A local the body only reads keeps one
definition that dominates the loop and needs nothing.

An assignment emits no instruction. It rebinds the local to the new SSA value,
which is why `total = total + i` followed by `print(total)` reads the *updated*
value — the "stale read" the matched shapes had to refuse outright.

### Four things the sketch missed

**If arms inside a body must inherit the backedge.** Falling off the end of an
arm is falling off the end of the body. Without it the arm returned and the loop
ran once; the fixture exited 0 instead of 10. This was a live miscompile, found
by running the program rather than by reading the diff.

**The backedge drops what the iteration allocated**, measured from the loop's
entry mark rather than the block's — the block carrying the backedge may be an
arm nested inside the body, whose own mark already includes values the iteration
made.

**The one-parameter jump encoding is mandatory.** The verifier rejects the wide
`jump_arguments` form for a single-parameter target; the argument has to travel
in `left` with the parameter's type. Two-carried-local loops worked while
one-carried-local loops did not, until both forms were emitted.

**Re-entering a definition through a backedge is not a double definition** when
the previous value was already consumed. The ownership verifier rejected any
redefinition on a path; it now rejects only redefinition while the value is
still *live*, which is the case that actually leaks. Without this no owned
allocation could exist inside a loop body at all.

### The permutation case stays declined

`a = b` then `b = a + 1` hands the header (b, a') — a permutation of its own
parameters. Breaking that cycle needs a scratch copy, and the only copy
instruction available is `IR_MOVE`, which the verifier reserves for owned
values. Declined rather than emitted in an order that loses one, and the
pre-existing skip fixture for that program still passes unchanged. An ordinary
counter loop never reaches it.

### The leak soak, finally runnable

The memory bar asks for a per-call leak to be caught by a loop under a leak
checker. Nothing in this path could run a loop before, so the string allocators
were checked by whole-run balance. Now:

| iterations | mmap | munmap | peak RSS |
|---|---|---|---|
| 20,000 | 20,000 | 20,000 | 256 KB |
| 200,000 | 200,000 | 200,000 | 256 KB |

Exact balance, and RSS **flat in the iteration count** across a 10× sweep.
Without the backedge drop the larger run would map roughly 800 MB. The 20,000
case is pinned in the gate and its counts are asserted under `strace`.

### Two negatives became positives

`bad_print_loop` and `bad_loop_print_stale` asserted that a print in a loop body
— and one reading a carried local an earlier statement had updated — must *not*
lower. Both lower correctly now, matching the stage-0 oracle line for line
(`tick tick tick` exit 0, and `0 10 30 60` exit 60). They are renamed to
`elf_loop_body_print_exit` and `elf_loop_print_update_exit` and moved to the
pinned-and-executed set. `bad_loop_print_guard` (a `break`) and `bad_print_if`
still decline, so the negative group did not simply evaporate.

### Evidence

| fixture | covers | Linux |
|---|---|---|
| `elf_while_sum_exit` | two carried locals, two sequential loops | exit 18 |
| `elf_while_guard_exit` | `if` inside a body — the backedge-inheritance bug | exit 10 |
| `elf_while_alloc_soak_exit` | owned allocation per iteration | exit 3, 20000/20000 |
| `elf_loop_body_print_exit` | print in a body | `tick`×3, exit 0 |
| `elf_loop_print_update_exit` | print reading an updated carried local | `0 10 30 60`, exit 60 |

All five are pinned by arena shape and SHA-256, checked against the stage-0
oracle, and executed on Linux x86-64. Mutating the backedge inheritance, the
body drop, or the exit restore each changes a pinned image, so all three are
held by the hashes rather than by inspection.

## Remeasuring the work list, and two things it was lying about

With loops landed I re-ranked the decline reasons rather than continuing down
the roadmap's order. The ranking was wrong in two ways, and fixing the
measurement was worth more than the next feature.

### The reasons were misattributed

`lower_typed_ir` is a fixpoint: a function calling one that could not lower is
*banned* and re-attempted next round. A banned function took a branch that
substitutes `items[0]` — the arena indexing needs some item — and every later
check then looked at the wrong item's body. So a banned function reported
whatever that item happened to say, almost always **"found no statements in the
body"**. Thirty-six functions were filed under a capability gap that does not
exist; they are simply waiting on a callee.

They now say "calls a function that could not be lowered" and skip immediately.
The reason list also names the function now, which is what turns a count into a
work list.

### Cross-file was two thirds of "parameter type is not bindable"

Stage 1 compiles one file at a time, so `zzzzzz_ir.mko` cannot see `Token`,
`ExprNode` or `StmtNode` — they are declared in siblings. Every parameter of
such a type types as `SEM_UNKNOWN` and fails `ir_cfg_bindable_type`.

Concatenating the package and compiling that measures the language gap without
the packaging artifact:

| measurement | split files | one package |
|---|---|---|
| lowered functions | 46 | 55 |
| "parameter type is not bindable" | 92 | 14 |

The remaining 14 are the genuinely ineligible structs — `StructLayout`,
`X86Code`, `X86Body`, `TypedIr` — which have slice fields. Every heavily-used
struct (`Token`, `IrInstruction`, `IrFunction`, `IrBlock`, `ExprNode`, …) is
already eligible, so struct eligibility is not the blocker it looked like.

**Multi-file packages are worth roughly nine functions**, not the bulk. That is
worth knowing before building them.

### The honest ranking, in package mode

| count | reason |
|---|---|
| 86 | needs a known type for every expression |
| 39 | supports only calls to declared functions and intrinsics |
| 36 | calls a function that could not be lowered (cascade, not a gap) |
| 34 | supports only scalar literal, unary, and binary expressions |
| 14 | parameter type is not bindable |
| 14 | assigns only non-owned scalars |

## `str_byte_at`

Picking off the top of reason 2. It reads a byte out of a string object and is
what the lexer and the symbol hash are built on — 22 call sites — and it was
missing from both the intrinsic table and the typer.

The contract is the interesting part: **out of range it returns -1, it does not
trap.** `mako_str_byte_at` yields -1 for a negative index or one at or past the
length, and the lexer reads that sentinel as end of input. A bounds trap here —
the obvious thing to copy from `x86_emit_index_byte` — would diverge from the
stage-0 oracle on every scan that deliberately walks off the end. It is typed
`int` rather than `byte` for the same reason: a byte cannot hold -1.

`elf_str_byte_at_exit` reads the first byte, one past the end, and a negative
index, so both sentinel paths are covered alongside a real read. Stage 0 and the
generated image both exit 79.

Package-mode lowering went 55 → 58.

### A probe that conflated two causes

The first `str_byte_at` probe also called `len` on a string, and when it failed
I recorded `len`-on-a-string as a second gap. It is not one: once `str_byte_at`
was typed, `len` on both a string and a slice compiled and ran correctly (both
exit 1 on Linux). Worth stating because the wrong note nearly became the next
task.

## `len` in the general path — all 39 of reason 2

Naming the missing callee in the diagnostic turned "39 declines for some missing
builtin" into one word: **every one of them was `len`**.

It had looked supported, because every probe that used it happened to match one
of the shape encoders, which handle `len` themselves. The shape-independent path
did not: it treated `len(x)` as an ordinary call, looked the name up in the
intrinsic table, found nothing and declined. `len` appears 1612 times in the
compiler's own sources, so this kept most of them out of the general path.

`len` is not a call the backend makes — it is one load of the header word, which
`IR_LEN` already encodes, and a slice header and a string object both carry
their length first, so one form serves both. The fix is entirely in the
lowering; the backend needed nothing.

`elf_general_len_exit` uses a nested guard tree, which no shape matcher accepts,
so it reaches the general path — and mixes slice and string lengths. Stage 0 and
the image both exit 34.

### Where those functions stop now

Reason 2 went to zero, and the functions behind it moved one gate further on:

| count | reason | was |
|---|---|---|
| 88 | needs a known type for every expression | 86 |
| 65 | supports only scalar literal, unary, and binary expressions | 34 |
| 36 | calls a function that could not be lowered (cascade) | 36 |
| 15 | assigns only non-owned scalars | 14 |
| 14 | parameter type is not bindable | 14 |
| 0 | ~~no intrinsic for the callee~~ | 39 |

The +31 on reason 4 is those same functions arriving at the next thing the
general path cannot lower: **indexing**. `xs[i]` is not in its expression set at
all. That is now the top item, and it is a bigger one than `len` — a struct-slice
element is a borrowed pointer rather than a loaded word, and `append(out, src[i])`
over a struct slice needs a stride-wide copy in the allocator path rather than a
one-word store. Package-mode lowering is unchanged at 58 for that reason: the
functions moved, they did not land.

## Indexing, and a silently dropped store

Adding `xs[i]` to the general path turned up a miscompile that had nothing to do
with indexing and predated it.

### The bug

A straight-line body — no branch, no loop — falls through every shape matcher to
the single-block path. That path lowers expressions in arena order, then emits
the statement intrinsics and the return. **An assignment statement emits
nothing.** Its right-hand side is lowered as an expression and the store is
never made.

```mko
fn main() {
    let mut i = 3
    i = 7
    exit(i)          // exited 3
}

fn main() {
    let mut xs: []int = make([]int, 3, 8)
    xs[1] = 40
    exit(xs[1])      // exited 0
}
```

Both are silent wrong answers. The first was found by running an indexing probe
against the stage-0 oracle and getting 5 where the oracle said 45.

It predates this work: removing the indexing change and rebuilding produced a
**byte-identical image** for the failing program, which is how it was confirmed
rather than assumed.

### The fix, and a first attempt that was too broad

A straight-line body containing a store now declines, which routes it to the
general path — where an assignment rebinds the local to its new SSA value and
works. The loop and merge-if shapes validate their own assignments and already
fail closed on an indexed target, so this was the only open case.

The first version keyed off `assignment_count`, which was the wrong counter: it
also counts void intrinsic *call* statements like `unsafe_ptr_store8(p, 65)`.
Those are fine — a call statement's effect happens while its expression is
lowered — and the over-broad guard broke `elf_unsafe_write`. A store is told
apart from a call by its second token, and is now counted separately.

Result: `i = 7` gives 7, and `xs[1] = 40` is refused outright with no output
file rather than compiled to something wrong.

### Indexing itself

`xs[i]` on a slice of int or byte is one bounds-checked load whose stride the
backend reads off the element type. A slice of **structs** is refused explicitly:
its element is a borrowed pointer rather than a loaded word, so admitting it
would hand the encoder an index to load at the wrong width — the same fail-open
that once made `pts[0].x` and `pts[0].y` compile identically.

Because a `make`d slice is all zeros, execution alone cannot show *which*
element was read, so the offset is checked the way that fail-open was caught:
compiling `xs[0]` and `xs[1]` must produce different images. Both checks are in
the gate.

| fixture | check | result |
|---|---|---|
| `elf_general_index_exit` | guards and a read through the general path | exit 190, matches oracle |
| `elf_general_index_exit` | `xs[0]` vs `xs[1]` images | differ |
| `elf_straight_line_assign_exit` | the dropped-store regression | exit 14, matches oracle |
| `bad_index_store` | indexed store | refused, no output file |

### Coverage went down, on purpose

Package-mode lowering **58 → 42**. Those sixteen functions were being
miscompiled: they contained straight-line assignments the single-block path
dropped. A refusal replacing a silently wrong answer is the trade this codebase
asks for, and the number should not be read as a regression.

### The new top of the list

| count | reason | was |
|---|---|---|
| 88 | needs a known type for every expression | 88 |
| 56 | assigns only non-owned scalars | 15 |
| 33 | cannot index a slice of structs | — |
| 28 | supports only scalar literal, unary, and binary expressions | 65 |
| 14 | parameter type is not bindable | 14 |

Reason 2 is now **assignment to an owned local** — `out = append(out, x)`, the
shape of every builder in the compiler. It is a rebind rather than a leak
(`append` consumes what it is given and returns the replacement), but getting it
wrong aliases one allocation to two bindings and double-frees, so it belongs in
its own increment with its own soak.

Also found and not fixed: `[]byte` is not bindable in the general path at all —
`SEM_ARRAY_BYTE` is absent from `ir_type_is_owned`, which is the same gap the
security review already lists as "[]byte ownership policy".

## The two structural gaps

### `[]byte` was never owned, so it always leaked

`SEM_ARRAY_BYTE` was absent from `ir_type_is_owned`. No `IR_DROP` was ever
emitted for a `[]byte`, so every `make([]byte, n)` leaked its block — **1 mmap,
0 munmap**, measured on the shipped `elf_make_byte_len` fixture. It also made
`[]byte` unbindable in the general path, which asks for a scalar or an owned
type and this was neither.

Adding it fixed the leak and exposed two more, both of which had been hidden by
`[]byte` not being owned at all:

**The matched path cloned owned arguments passed to intrinsics.** Only a
declared Mako function takes ownership of what it is handed; an intrinsic either
borrows (the whole `unsafe_*` family reads through a slice and returns) or
consumes and hands back the replacement (`append`). Cloning for either leaks the
copy, because nothing on the other side drops it. Clone is now emitted only for
a declared callee — which also made `elf_int_append_exit` *smaller*, 1136 → 873
bytes and 3 allocations → 2.

**A borrowing intrinsic was modelled as consuming.** That is wrong in both
directions: the value was never dropped, and reusing the slice afterwards was
rejected as a use-after-move when it is perfectly valid — `let n =
unsafe_write_bytes(1, xs); return len(xs)` did not compile.

`ir_call_consumes_arguments` now decides it. The rule is deliberately confined
to **slices**: a first version applied it to strings too, and `lexer.mko` and
`zz_symbols.mko` stopped verifying, because `str_byte_at(s, i)` on a string
*parameter* then left that parameter live and demanded a drop the callee must
never emit — a borrowed parameter belongs to its caller. Strings keep the old
consuming behaviour.

Result, all measured on Linux under `strace`, with every exit status unchanged:

| fixture | before | after |
|---|---|---|
| `elf_make_byte_len` | 1 / 0 | 1 / 1 |
| `elf_make_byte_append` | 1 / 0 | 2 / 2 |
| `elf_unsafe_argv_copy` | 1 / 0 | 1 / 1 |
| `elf_unsafe_bytes_to_string` | 2 / 1 | 2 / 2 |
| `elf_open_byte_slice`, `elf_read_byte_slice`, `elf_write_byte_slice` | 1 / 0 | 1 / 1 |
| `elf_int_append_exit` | 3 / 3 | 2 / 2 |

Ten pinned images changed and were re-verified against the stage-0 oracle before
being re-pinned, not simply updated.

### One file at a time

Stage 1 had no way to compile more than one source, and no module system, so a
type declared in one file was invisible in the next: `zzzzzz_ir.mko` alone
cannot see `Token` or `ExprNode`, and every parameter of such a type typed as
`SEM_UNKNOWN`. That was **92 of the 106** "parameter type is not bindable"
declines.

`read_package` takes a comma-separated path list and concatenates the members,
separated by newlines, into one buffer. Spans then index that buffer, so this is
one span space and one symbol table with no arena merging — the tradeoff is that
a diagnostic's offset is package-relative rather than file-relative. A path with
no comma is one file and behaves exactly as before, so every pinned single-file
invocation in the gate is untouched.

It fails closed on each member independently. `read_file` returns "" for a
missing path, a directory and a broken symlink alike, so checking only the total
would let a mistyped member vanish into a package that still compiled without
whatever it declared. The gate asserts that a package with an unreadable member,
a package of one missing file, and an empty path list are all refused.

The compiler's own fourteen backend files now compile as one unit: **294
signatures, 43 lowered**, pinned in the gate as a floor rather than an exact
count so a gain does not force a re-pin but a silent fall still fails.

## Owned rebinding, `make`, and block-scoped bindings — 43 → 53

The builder shape — `let mut xs = make(...)` then `xs = append(xs, v)` — is how
every arena in this compiler is written, and it needed three things.

**Storing over an owned local retires what it held.** `xs = append(xs, v)` hands
the old allocation to the call, which consumes it and returns the replacement;
dropping it again would be a double free. `xs = make(...)` hands it to nobody,
so without a drop it leaks. Which happened is read off the instructions the
right-hand side just emitted, via the same borrow-aware
`ir_instruction_range_consumes_value` — not guessed from the shape of the
expression. The retired value leaves the live set either way.

**`make` had no lowering in the general path at all**, so a typed slice `let`
declined before any rebind was reached. It is one allocation whose length is the
literal after the type — the same reading the matched path uses, so the two
cannot disagree about how big a `make` is. The type-reference node and the inner
links of a `make` chain emit nothing.

**Bindings had to become block-scoped.** This is the one that mattered.
`owned_locals` was a single array threaded through the whole function, so a
store in one `if` arm stayed visible in its sibling — and the sibling then
referenced a value defined in a block that does not dominate it. It surfaced as
`IR left value does not dominate its use` on `x86_emit_int_rax`, whose
`out = append(out, ...)` appears in three separate arms. Each pending list now
carries a snapshot of the bindings it was reached with, restored when its block
is lowered. The loop header patches the body's snapshot with its block
parameters, because that snapshot was taken at the `while`, before the
parameters existed — without it the body reads the pre-loop value and the loop
tests the same thing forever.

Scalar assignment could have hit the same dominance bug; owned rebinding is
simply what made it reachable.

Verified: `elf_owned_rebind_exit` exits 43 against the stage-0 oracle with
**6 mmap / 6 munmap**, and the whole loop and assignment fixture set re-run on
Linux unchanged. Package-mode lowering **43 → 53**.

### One thing the balance numbers do not mean

A program whose last statement is `exit(n)` shows one allocation outstanding —
`exit` terminates before scope-exit drops run. `elf_general_len_exit` is 2/1 for
that reason and always was; the kernel reclaims it. Only a program that returns
normally, or one that allocates inside a loop, gives a balance worth reading,
which is why the soak fixture is the one that carries the leak guarantee.

## First register work: eliding the round trip through the frame

The slot model gives every SSA value a frame slot, so a value produced and
immediately consumed makes a store-then-load trip through memory it does not
need. Measured before writing anything: **15 of 38 frame loads** across the
pinned images were a store followed straight away by a load of the same slot.

The emitter now tracks which slot rax still holds and drops the load when it
already holds it.

### Why a byte-level peephole would have been wrong

The obvious version — scan the finished stream for `mov [rbp-k], rax` followed
by `mov rax, [rbp-k]` and delete the second — is unsafe here, twice over.
Displacements are patched after layout from recorded offsets, so removing bytes
shifts every one of them; and emitters like `x86_emit_index_byte` contain their
own internal relative jumps around the bounds trap, which a deletion inside
their range would silently break.

The elision is therefore done at the **head of an instruction's emission**,
where both hazards vanish: an emitter's internal jumps are self-contained, so
none can target bytes emitted before that instruction began, and a block start
is the only thing a real jump ever targets. Call displacement positions recorded
within the instruction are shifted by the four bytes removed. Only the disp8
form is recognised — a wide frame keeps its round trip rather than risk a wrong
match on a disp32 encoding.

### Evidence

The change touches every generated image, so a pin update alone would prove
nothing. Two compilers were built from the same sources, one with the elision
disabled, and all **125 testdata fixtures compiled with both and executed on
Linux x86-64 with and without arguments**:

| | |
|---|---|
| identical exit status and stdout | **248 of 248 runs**, both increments |
| real divergences | **0** |

The two apparent divergences were the harness: `elf_arg_get` and
`elf_unsafe_argv_copy` echo and measure `argv[0]`, which differed because the
two builds sat in directories of different name lengths. Re-run from
equal-length paths they agree.

| measure | before | after |
|---|---|---|
| frame loads | 870 | 753 (**13.4% removed**) |
| frame stores | 1013 | 938 (**7.4% removed**) |
| total frame traffic | 1883 | 1691 (**10.2% removed**) |
| image bytes | 52923 | 52155 (1.5% smaller) |

The store goes too when the value has no other reader. A single-use temporary
then never reaches the frame at all: it is produced in rax and consumed from
rax. That needs whole-function liveness — every instruction, call argument and
jump argument is scanned, in any block, reachable or not — because the cost of
missing one use is a slot that was never written and a later read of whatever
the frame held.

Two restrictions keep it honest. The store is only dropped for opcodes whose
emitters are straight-line and finish with it (`IR_CONST`, `IR_BINARY`,
`IR_UNARY`, `IR_MOVE`, `IR_LEN`); an emitter with an internal jump — the bounds
trap in an indexed load — can target the bytes after its store, so removing it
would retarget that jump into the middle of something else. And the *consumer*
is excluded from the liveness scan, since its read is precisely the one already
satisfied out of rax; counting it was why the first version removed no stores at
all.

### What this is not

This is one peephole, not a register allocator. Every value still lives in a
frame slot and every operand that is not the immediately preceding result is
still a memory reference. A real linear-scan allocator over the general CFG is
still ahead, and it is the precondition for the self-hosted backend competing
with LLVM on anything.

### Corpus effect: none yet

Measured after landing: stage 1 still lowers **46** functions across
`compiler/*.mko`, the same as before. That is the expected result and worth
stating rather than leaving to inference — the compiler's own concatenation
sites sit inside functions that decline for other reasons (loops, unmodelled
statement forms), so unblocking concatenation does not by itself lower any of
them. The capability is proven by fixtures, not by the corpus count.

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
