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

## W1 — Call result types (22)

    CFG lowering needs a known type for this call

The largest genuine cause. The lowerer will not emit a call whose result type it
cannot name. Since 49 more functions are blocked behind *some* callee, this is
also the most likely single unlock for the cascade.

Start by grouping the 22 by callee: if they concentrate on a few signatures, this
is a resolution gap for particular return shapes rather than a general problem.

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
