# Self-host work list

Ranked, measured, and re-runnable. This is the "what to do next" list; the
architectural notes live in SELFHOST_CRITICAL_PATH.md.

## Snapshot

    344 signatures in the compiler package
    175 lowered to verified typed IR   (51%)
    169 skipped
      0 bytes of machine code emitted for the compiler itself

## Regenerate this report

Stage 1 prints the work list when given a fourth argument; the gate never
passes one, so pinned output is unaffected.

    PKG="compiler/lexer.mko,compiler/parser.mko,...,compiler/elf64.mko"
    ./makoc-stage1 "$PKG" /tmp/pkg.elf why \
      | grep ": " | sed 's/^[^:]*: //' | sort | uniq -c | sort -rn

Each line of the raw output is `function_name: reason`, so the same output
also tells you *which* functions a given fix unblocks.

## How to read the ranking

`calls a function that could not be lowered` (49, 29%) is not work — it is
cascade. Those functions are blocked only because a callee is blocked, and they
unblock for free as the real causes are fixed. Treat 120 as the true backlog and
expect the count to fall faster than the items you close.

The rest cluster into four groups. They are ordered below by unblock-per-effort,
not by raw count.

---

## W1 — Call result types (22) — root-caused: struct drop-mask width

    CFG lowering needs a known type for this call

**This is not a type-inference gap.** Traced end to end:

    call result type <- semantic_declared_type(signature.result_type)
                     <- named struct -> struct_layout_constructible(name)
                     <- layout.constructible[i] = one_slot and maskable
                     <- maskable = count <= 15 and slice_fields <= 3

A struct is only constructible if its drop mask fits, and the mask is packed
into a single int: 15 slots x 2 bits (bits 0-29) plus 3 stride overrides x 10
bits (bits 30-59). The compiler's own aggregates exceed that, so their declared
type is SEM_UNKNOWN and every call returning one is refused.

`TypedIr` fails on three counts at once:

| limit | cap | TypedIr |
|---|---|---|
| flattened slots | 15 | 16 |
| struct-slice fields (overrides) | 3 | 5 |
| `[]string` field | no code exists | `string_literals` encodes as *not owned* |

The 2-bit code space is already full — 0 none, 1 `SEM_STRING`, 2 `SEM_ARRAY_INT`,
3 `SEM_ARRAY_BYTE` — so `[]string` cannot be added without a third bit per slot.
And the budget is spent: 20 slots plus 3 overrides needs 70 bits. There is no
cap raise that fits; `TypeInference` (21 fields) is worse than `TypedIr`.

Checked for a cheap subset and there isn't one: `IrVerification` (4 fields) and
`LexResult` (5) are already constructible, so the blocked calls are exactly the
ones returning the large aggregates.

**The fix is a representation change**: carry a descriptor index in the drop
instruction's `aux` and hold the real per-slot codes in a side table, instead of
packing a bitmask into one integer. Consumers are few —
`struct_owned_slot_mask` (produces), `ir_drop_aux` (passes through), and
`x86_emit_drop_struct_fields` (decodes) — but every struct drop changes, so many
pinned images move and each must be executed on Linux before it is re-pinned.

**Why it is still first.** It is the one item that plausibly clears the 49-strong
cascade as well as its own 22 — up to 71 of 169, ~42% of the backlog — and
self-hosting cannot happen without it: a compiler that cannot compile its own
core types cannot compile itself.

## W2 — If/return shape (40 across four reasons)

    12  requires both if arms to hold statements
    10  could not resolve a return value
     9  requires every path to return
     9  requires a boolean if condition

Four separate refusals around the same construct. An empty `else`, a path that
falls off the end, and a non-`bool` condition each refuse the whole function.
`requires both if arms to hold statements` in particular looks like a template
artefact — an empty arm is a legal program, not an unsupported one.

These are the reasons most likely to be *deleted* rather than fixed when general
CFG/SSA construction lands, so check W2 against that plan before investing: if
the general path is close, fixing these twice is wasted.

## W3 — Loop-carried values (15)

     7  carries at most one owned value through a loop
     5  cannot carry a loop value that permutes another carried value
     3  carries only owned slices through a loop

Real limits in the block-parameter machinery, not shape templates. The permuting
case needs a scratch move across a multi-argument jump (the second write can
clobber the first), which is the same missing capability the loop-exit
trampoline needs. Doing W3 alongside the loop-exit block parameter work in
SELFHOST_CRITICAL_PATH.md shares most of the effort.

## W4 — Types and literals (18)

     7  requires a literal make length or a lowered integer length
     5  indexes only slices of int or byte
     3  supports only scalar literal, unary, and binary expressions
     3  supports only integer, boolean, and string literals

Narrow, independent, each small. Good work to interleave when a larger item is
blocked or waiting on a gate run.

## W5 — Long tail (~10 reasons, 1–4 each)

Includes the two deliberate refusals recorded in SELFHOST_CRITICAL_PATH.md
(`break after a carried-local update`, and owned struct slices via
`parameter type is not bindable`). Do not close these by relaxing a guard —
both replaced miscompiles that hash pins had preserved.

---

## The cheapest lever, before any of the above

Some of these functions can be rewritten in a simpler Mako subset instead of
teaching the backend a new construct. An empty `if` arm, a non-`bool` condition,
or a non-literal `make` length in the *compiler's own source* costs nothing to
change and unblocks the same function.

Use the raw report to find which functions each reason names, then judge per
function: teach the compiler, or simplify the source. Bootstraps normally
constrain their own sources to a deliberate subset for exactly this reason. This
is the only item here that buys lowered functions with no compiler changes at
all.

## Sequencing

    W1  ─► measure cascade drop ─► reassess
    W4  interleave freely (independent, small)
    W3  do with the loop-exit trampoline (shared machinery)
    W2  only after deciding whether general CFG/SSA supersedes it

Re-run the report after each item. The cascade count falling faster than the
item count is the signal the ordering is right.

## Gate discipline for every item

Ratchets, so progress cannot silently reverse:

* `ir_functions` must never fall (the gate floors it; raise the floor as it
  climbs).
* Every image whose bytes move must be executed on Linux before it is re-pinned
  — the pins are hashes, and a byte-stable image can still be wrong.
* Peak RSS must not regress more than 10%; the package run is at 8.44 GB and is
  a gating item for the fixed point, not a cleanup.

### W1 implementation: two routes, measured

The backend needs per-slot drop codes for a struct it can already identify —
`struct_kind_index(instruction.type_kind)` names it at the drop site. The layout
already holds the codes as `layout.slot_type[layout.field_start[i] + slot]`, so
no new table has to be computed; only transported. Two ways to transport it,
with real blast radius:

**Route A — thread the layout arrays to the backend.**
Pass `slot_type` and `field_start` from `main.mko` (which has `source`, tokens
and items, so it can call `build_struct_layout`) into
`elf64_lower_typed_ir`, then down to the drop site. Honest cost:

    x86_emit_scalar_segment   27 call sites
    x86_lower_scalar_body      2
    x86_encode_function_body   2
    x86_lower_functions        2
    x86_emit_drop_struct_fields 2
    elf64_lower_typed_ir + main  2

~35 edit points across four files. Every one is mechanical and a missed one is
a build error, not a silent bug — but it is a large single change.

**Route B — carry the codes in the existing `constants` arena.**
`constants` is already threaded to the drop site, so nothing new is passed.
Emit the per-struct code runs into the arena and let `ir_drop_aux` return the
run's index instead of a packed mask. Roughly five edits instead of thirty-five.

The catch is pin churn: `ir_constants` appears in every pinned fixture summary,
so adding entries moves fixtures that have no structs at all. Placing the table
at the end of the arena keeps existing indices stable but leaves `ir_drop_aux`
unable to compute the base, which is the thing that makes Route B cheap. Worth
resolving before choosing — if the base can be derived (for instance by
emitting the table only when the layout has an owned-field struct, and passing
its base once), Route B is much the smaller change.

**Either way**, the last step is the same and is where the value is: relax
`maskable` in `build_struct_layout` from `count <= 15 and slice_fields <= 3` to
the type encoding's own limit (`count < SEM_STRUCT_WIDTH_UNIT`), and add a code
for `SEM_ARRAY_STRING`, which a table can represent and a 2-bit field cannot.

**Verification is not optional here.** Every struct drop changes shape, so many
pinned images move. Each moved image must be executed on Linux before it is
re-pinned — this is the exact failure mode that let `argc()` return 0 and
`i + i` read uninitialised stack behind matching hashes.
