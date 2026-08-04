# Self-hosted native compiler

The full target and acceptance contract are documented in
[FULL_SELF_HOSTING_MANDATE.md](FULL_SELF_HOSTING_MANDATE.md), with staged work
tracked in [SELF_HOSTING_ROADMAP.md](SELF_HOSTING_ROADMAP.md) and an
implementation plan in
[FULL_SELF_HOSTING_EXECUTION_PLAN.md](FULL_SELF_HOSTING_EXECUTION_PLAN.md).
The live evidence log is
[SELF_HOSTING_PROGRESS.md](SELF_HOSTING_PROGRESS.md). This document's bring-up
notes are status evidence only; they do not constitute the full self-hosting
claim.

Mako's production compiler is intended to be written in Mako and to emit
native object code directly. The existing Rust implementation and C backend
are stage-0 bootstrap tools and behavioral oracles; they are not the final
compiler architecture.

## Bootstrap contract

1. **Stage 0** (the existing compiler) builds `compiler/main.mko`.
2. The resulting **stage 1** compiler builds the same compiler sources.
3. The resulting **stage 2** compiler rebuilds those sources again.
4. Stage 1 and stage 2 must agree semantically. Reproducible builds additionally
   require byte-identical artifacts after normalizing explicitly documented
   build metadata.

A compiler is not called self-hosted until stage 1 successfully produces a
working stage 2. A Mako wrapper around the Rust compiler, generated C, Clang,
LLVM command-line tools, or another compiler does not satisfy this contract.

## Required compiler pipeline

```text
.mko source
  -> Mako lexer/parser
  -> resolved, typed Mako AST
  -> ownership/NLL and concurrency validation
  -> target-independent SSA IR
  -> optimization passes
  -> target instruction selection and register allocation
  -> ELF / Mach-O / COFF object writer
  -> platform linker interface
  -> executable machine code
```

The runtime ABI is versioned separately from the compiler. Runtime components
needed by every binary must migrate from header-only C into native Mako or
small, documented platform ABI shims. Platform linkers and operating-system
ABIs are allowed; translating user programs to C or Rust is not.

## Backward-compatibility gate

The self-hosted compiler must accept existing source and preserve:

- language syntax, type rules, ownership/NLL behavior, overflow and bounds
  semantics;
- package manifests, lockfiles, imports, visibility, and C/plugin ABI;
- CLI commands, exit codes, diagnostics schemas, and program arguments;
- concurrency (`crew`, `kick`, channels, select), parallel `fan`, cancellation,
  and race-safety guarantees;
- native targets, WASI behavior, standard library, networking, database, TLS,
  GPU, and optional-library feature detection.

Differential tests compile and run the same fixture with stage 0 and the new
compiler and compare stdout, stderr, exit status, and observable side effects.
Negative fixtures must fail in the same compilation phase with compatible
source locations. Intentional changes require a language-version boundary and
a documented migration; they are never smuggled into the backend switch.

## Performance gate

"Faster than C and Rust" is evaluated per published workload, not asserted as
a universal property. Every result records source, compiler versions, flags,
hardware, warmup, sample count, variance, peak RSS, allocation count, binary
size, and compile time. Performance work cannot weaken safety checks unless the
source uses an existing explicit unsafe operation.

The compiler itself is designed around compact span-based tokens, arena-backed
IR, iterative graph walks, parallel function compilation, deterministic merge,
and bounded-memory incremental caches. Generated programs are evaluated for
latency, throughput, scaling across cores, peak/live memory, and tail latency.

## Current bring-up status

`compiler/lexer.mko` tokenizes into compact byte spans without allocating
identifier/literal payloads. `compiler/parser.mko` builds a flat structural AST
for top-level declarations and validates delimiter balance.
`compiler/z_expression.mko` now has a flat Pratt arena for literals,
identifiers, unary and precedence-aware binary operators, calls, indexes, and
field selection. `compiler/z_type.mko` adds a flat child-edge arena for named
and qualified types, arrays, maps, tuples, function types, and both bracket and
angle-bracket generics. Stage 0 can build the frontend and the resulting
stage-1 executable can process ordinary programs, concurrency syntax,
representative declaration forms, and every current self-hosted frontend
source. Top-level function parameters and return types are now validated into
the shared type arena and retained as compact signature records, including
source-located rejection of malformed signatures. Function bodies are retained
in a second flat arena with child-edge ranges for conditionals, loops, match,
select, deferred and concurrent blocks, bindings, returns, and expression
statements. Remaining expression forms, method/extern/data declaration type
integration, complete semantic analysis, full CFG/SSA lowering, object writing,
and the stage-1/stage-2 fixed point remain required before the self-host claim
is met.

Supported scalar, call, index, full-slice, field, tuple, array-literal,
struct-literal, and struct-update expressions are now attached to their
statement nodes; unsupported expression forms retain `-1` roots and their full
token spans for incremental bring-up.
Typed `make([]T, len, cap)` and `make(map[K]V, hint)` construction expressions
reuse the type parser and retain their type token ranges. Generic struct
constructors and typed calls/channels use the same type-application path while
remaining disjoint from ordinary indexing. The literal fixture asserts a
deterministic 79-node expression arena through
the stage-1 executable. The stage-0 gate currently reserves a
64 MiB compiler worker stack because stage 0 still uses recursive typed/codegen
walks over the monolithic bootstrap package. This is virtual stack reservation,
is overrideable with `MAKO_SELFHOST_STACK_MB`, and is not part of stage 1's
flat, iterative architecture.

Semantic bring-up now includes an open-addressed global symbol index. It hashes
identifier bytes directly from source spans, resolves collisions by byte-span
comparison, allocates no identifier strings during lookup, and rejects
duplicate top-level declarations with source locations. The gate includes both
an intentional hash collision and a duplicate-definition negative fixture.
Global identifier expressions now retain indexes into that table through a
parallel integer arena. The resolver binds functions, constants, and type
constructors without embedding pointers; builtins remain `-1` for later
intrinsic resolution. The rich fixture asserts three resolved constructor
references.

`let`/`var` bindings receive local IDs in a separate arena. Resolution observes
declaration order, excludes a binding from its own initializer, and records
local shadowing without mutating the global-symbol arena. Statement nodes
retain exact enclosing block spans, so nested locals resolve in the narrowest
containing scope without leaking across `else` arms or after their block. The
rich fixture asserts ten local bindings and ten resolved local uses.
Function parameters now retain source spans, mutability, and type-arena roots,
then enter the lexical binding arena before body locals. Duplicate parameters
are rejected with source locations; the concurrency fixture currently retains
one parameter and resolves two uses through it.

A bounded, iterative semantic pass now stores primitive expression and local
types in parallel integer arenas. It propagates `int`, `float`, `bool`, and
`string` through literals, parameters, local initializers, unary and binary
operators, homogeneous arrays, indexing, slicing, selected intrinsics, and
calls to declared functions with scalar, primitive-array, or map result types.
Array results retain their element type through indexing, and multi-argument
call chains reuse the resolved root callee's signature, while unresolved calls
stay explicitly unknown. The rich fixture currently infers 38 expression
types. Resolved function calls now validate terminal call-chain arity and every
known argument type against declared parameter edge ranges, with source-located
diagnostics for mismatches. Unknown argument types and unresolved callees remain
accepted for incremental compatibility. Unknown types remain an explicit zero
value for incremental compatibility. Return statements are also checked across
nested statement arenas: declared results require a value, result-less
functions reject returned values, and known return types must match the
signature. Unsupported expression types remain deferred while nested composite
unification, user-defined result types, control-flow completeness, and
ownership facts are brought online. Known scalar operands now follow stage-0
operator rules: numeric arithmetic, string concatenation only with `+`,
compatible equality, numeric ordering, boolean logic, integer bitwise
operations, and type-specific unary operations. Unknown operands remain
deferred rather than producing speculative diagnostics. Attached `if` and
`while` conditions with known types must now be boolean; unsupported condition
forms remain deferred until their expression nodes are available. Array literal
chains reject known heterogeneous elements without materializing element
vectors. Known primitive-array and string indexes must be integers, slice
bounds are checked independently as low/high/max positions, and valid slicing
preserves the inferred array type. Retained type-token spans now restore scalar
array and map types through `make`: arrays require an integer length and accept
one integer capacity, while maps accept at most one integer size hint. Terminal
make-chain validation prevents argument prefixes from producing false arity
errors. Unshadowed `len`, `cap`, and `format_int` calls now require exactly one
argument and validate known container/scalar types with stage-0-compatible
diagnostics. Resolved globals or locals with those names continue through
ordinary signature/local handling rather than being mistaken for intrinsics.

`compiler/zzzzzz_ir.mko` now defines the first target-independent typed IR:
flat function, block, instruction, successor, expression-value, and local-value
arenas with stable integer IDs and source-token provenance. The initial lowerer
accepts only fully typed straight-line functions containing bindings and
returns. It emits typed parameter values, aliases resolved locals directly to
SSA values, lowers scalar expression dependencies, and ends each entry block
with an explicit return. Functions containing control flow, side-effect-only
statements, or unknown types are counted as skipped instead of being
miscompiled as linear code. The integration fixture deterministically lowers
two parameters and one addition into three SSA values and four instructions.
CFG construction, block parameters, branches, ownership/drop operations,
concurrency operations, and optimization passes remain the next IR milestones.
Every emitted IR package is now checked by a linear verifier before stage 1
reports success. It validates function/block/value ranges, unique function and
block ownership, final terminators, intra-function successors, SSA
use-before-definition, unique result definitions, known result types, and
complete instruction/value coverage. A deliberately malformed SSA fixture
with a dangling operand must be rejected by the verifier smoke gate.
IR constants now live in a separate typed pool rather than referring back to
AST literal nodes. Decimal integers, floats, and booleans carry normalized
payloads; strings retain immutable content spans to avoid ownership-heavy
copies during bootstrap. `IR_CONST` stores a checked pool index, the verifier
requires matching types and referenced entries, and decimal overflow fails
lowering before host arithmetic can wrap or trap. The integration fixture
covers all four payload classes.

The first control-flow lowering paths now accept deliberately narrow,
side-effect-free `if` shapes: either a returning then arm followed by a
returning fallthrough, or an `if/else` whose two arms both return. Ordered child
edges identify the true and false return blocks without guessing an arm
boundary. The entry block retains the branch condition and ends in `IR_BRANCH`
with exactly two same-function successors; each arm retains its own typed
expressions. The form uses two return blocks and deterministically produces three
blocks, seven instructions, four SSA values, two constants, and two successor
edges. Scalar calls and owned call results may appear in either lowered arm and
retain explicit call-site/argument metadata. Arm-local expressions are emitted
only in their owning block rather than speculatively in the entry block, so an
owned result exists and is consumed only on the selected path. Allocation,
arm-local ownership transfers in control flow, nested control flow, empty arms,
and ambiguous statement shapes remain skipped. The verifier enforces boolean branch
conditions and exact branch/return successor counts.

Returning `if/else` arms now converge through the first typed SSA block
parameter. Each arm ends in `IR_JUMP` carrying one typed edge argument; the
fourth block begins with `IR_BLOCK_PARAM` and returns that merged value. The
fixture deterministically produces four blocks, nine instructions, five SSA
values, two constants, and four successor edges. The verifier requires the
parameter to be first and unique, rejects parameters on entry blocks or with no
predecessors, and checks that every incoming edge supplies exactly one argument
with the same type. A malformed missing-argument graph is rejected by the
self-hosted verifier smoke gate. Constants and arithmetic belonging to each
return expression are emitted in that arm rather than speculatively evaluated
in the entry block. A second malformed graph proves that source order cannot
leak a value from one sibling arm into another: an operand defined on one arm
never dominates the other. Multiple block parameters remain future extensions.

A second CFG shape introduces `IR_JUMP` and an explicit merge block for a pure
`if/else` whose arms contain one local binding each and whose fallthrough
returns an entry-defined value. The entry branches to two arm blocks, both
jumps target the same return block, and the verifier requires one same-function
successor per jump. Arm-local initializers stay in their respective blocks. The
merge fixture emits four blocks, ten instructions, six SSA values, four
constants, and four successor edges. Values
defined independently in both arms are not allowed to escape their lexical
scopes; the current single block parameter is used to merge return values.
General multi-value merge construction remains required.

The first loop shape now lowers: one scalar `while` preceded by leading
bindings and followed by a return, whose body is one or more reassignments of
distinct pre-loop integer locals (the carried locals). Each carried local
becomes an `IR_BLOCK_PARAM` on the header block — one parameter per carried
local, in declaration order: the entry block jumps in with the initializers,
the header evaluates the boolean condition and branches to the body or the
exit, and the body jumps back with the updated values. Jumps into a
single-parameter block keep the original encoding (argument in `left`); jumps
into a multi-parameter block carry their arguments in a new `jump_arguments`
arena indexed by the jump's `aux`. The condition must reference a carried
local, and every right-hand side must reference one, but a right-hand side
may only read header parameters — never the value an earlier body statement
assigned to another carried local, because the expression arena is not in
source order and such reads cannot be resolved reliably (they stay skipped).
The body may also hold one guard `if` whose only child is a `break` or
`continue`; its condition may only read carried locals no earlier body
statement assigns, since their header parameters are still current at that
point. A guard adds blocks between the header and the updates: pre-guard
updates, the guard branch, post-guard updates, and the arm. A continue arm
jumps back to the header carrying each local's value as of the guard (seven
blocks). A break can fire after pre-guard updates changed a carried local, so
the exit block gets its own parameters — the header's false edge reaches them
through a trampoline jump (a branch cannot carry arguments), and the break
arm carries the current values (eight blocks); post-loop reads resolve to the
exit parameters. The body may instead hold one if/else whose arms contain only
assignments (a merge-if): every local an arm assigns gets a block parameter on
the merge block, fed by the arm's update on one side and the value current at
the if on the other; a post-if assignment to a merge local overrides the merge
value on the backedge. Arm right-hand sides read only header parameters —
never a pre-if update or an earlier same-arm update — and the if condition
follows the guard's rule. Integer-returning calls may appear in the while condition
and in assignment bodies (guard conditions stay call-free so their compare
can stay fused); call results are ordinary SSA values in the slot model, and
call arguments follow the same dominance rules as every other operand.
Anything else — nested control flow, repeated targets, non-integer locals, or
a missing trailing return — stays skipped.
Verifier dominance is now a real per-function fixpoint
(`dom(b) = {b} ∪ (∩ dom(p))` over predecessors, one bitmask per block, capped
at thirty-two blocks per function) applied to left/right operands and call
arguments, replacing the same-block-or-entry rule; the loop shape verifies
with the header parameter used from three different blocks. The existing
dominance negative fixture is still rejected because its definition block is
absent from the use block's dominator set.

The first ownership-aware IR operations are now explicit. Strings, primitive
arrays, and maps are classified as owned values. `IR_MOVE` transfers an owned SSA value to
a fresh value before it leaves a function, while `IR_DROP` destroys live owned
values that reach scope exit. Array construction and slicing transfer their
owned inputs along the construction chain, and an owned return consumes the
final moved value. Cleanup is emitted in reverse value order. The verifier
treats these values as linear capabilities: every owned definition must be
consumed exactly once, and it rejects malformed ownership operations,
use-after-move, leaked owned values, and double-drop. Positive fixtures cover
both returning and discarding an integer array; internal malformed graphs cover
use-after-move and double-drop. Ownership verification now follows every
reachable CFG path with explicit undefined/live/consumed states. Returning
`if/else` arms drop the unselected owned parameters and transfer the selected
array through the merge parameter, allowing mutually exclusive cleanup without
false double-drop reports. The owned-CFG fixture verifies both paths and their
exact instruction layout. General call ownership, nested CFG cleanup, and
borrow lifetimes remain future ownership milestones. String literals now
receive the same deterministic move/drop treatment as containers. Constructing
`[]string` consumes each owned element into the array chain, leaving one final
array owner to move or drop; exact fixtures cover string return, string discard,
and nested string-array cleanup.

Single-token statement expressions use the atom parser directly. This keeps a
valid condition such as `if flag { ... }` from being reinterpreted as the start
of a struct literal, and IR lowering now range-checks condition roots before
accessing the expression-value arena.

Calls now use explicit flat `IrCallSite` and call-argument arenas. `IR_CALL.aux`
indexes a call site containing the stable callee item ID and a contiguous
argument range, while instruction operands remain free for ordinary SSA ops.
This represents zero, one, or many positional arguments without chained call
instructions. The verifier requires exactly one instruction per call site,
complete non-overlapping argument coverage, a corresponding lowered callee,
matching arity and parameter types, same-function arguments, and dominance at
the call. Owned arguments are transferred by value and become consumed on that
CFG path. A fresh owned expression or a `hold` local is transferred directly.
Passing an ordinary owned local emits `IR_CLONE`, so the callee owns an
independent value and the original remains valid for later use; the cloned owner
is removed from scope cleanup after the call so it cannot be dropped twice.
Using a `hold` local after its transfer is rejected by the path-sensitive
ownership verifier. Owned results participate in normal move/drop insertion.
Exact and internal fixtures cover scalar calls, owned string arguments and
results, two owned arguments in stable source order, cloning an ordinary string
local, and rejecting a transferred `hold` local. `share` currently uses the safe
clone fallback; reference-counted sharing and explicit borrow parameter modes
remain future call-ownership milestones.

Owned local-to-local bindings follow the same source-ownership rule. Reading an
ordinary or `share` owner into a new binding emits `IR_CLONE`, while reading a
`hold` owner emits `IR_MOVE` and invalidates the source. This prevents two local
names from silently sharing one linear SSA owner. The verifier rejects reuse of
the moved source, and CFG cleanup drops each distinct cloned owner exactly once.

Scalar calls and calls returning owned values are also supported in the current
single-`if` CFG forms. Their arguments must dominate the call within the same
arm or from the entry block, and the usual call-site, arity, type, and linear
ownership checks still apply. An owned result returned by an arm is consumed by
that arm's `IR_RETURN` or `IR_JUMP`, and definitions on the unselected arm never
execute. Owned parameter arguments use an arm-local `IR_CLONE`: the selected
arm transfers the clone to the callee, while every return arm emits exactly one
drop for the caller's original parameter. Fresh string and owned call-result
arguments are also arm-local and may transfer directly. Direct transfers from
an entry-defined `hold` local are path-sensitive: the call arm consumes the
unique owner directly, while each non-consuming return arm emits one `IR_DROP`.
Other entry-defined owned locals retain clone semantics and are dropped on each
return path. The statement-merge `if/else` form now drops every unconsumed owned
definition in its originating arm before `IR_JUMP`; exact coverage includes an
owned call result in both arms. Transfers of arm-local bindings beyond their
originating statement remain excluded until their local scope state is
represented explicitly.

`compiler/x86_64.mko` and `compiler/elf64.mko` now form the first direct
backend slice: deterministic x86-64 machine encoding (constant return,
constant add, conditional return on a SysV `edi` selector or a signed 64-bit
`cmp rdi, imm` condition, and a Linux process-entry bridge that loads `argc`
from `[rsp]` into `edi`, calls the lowered body, and exits with its `eax`
result) plus a minimal static ELF64 executable writer with one read/execute
`PT_LOAD` segment and no section table. Comparisons use full-width signed
predicates so Mako `int` parameters are never truncated to 32 bits. Verified
IR lowers to an executable in three narrow shapes: a single integer constant
return, a constant add return, and a single-parameter branch whose arms
produce integer constants — both the three-block direct-return form and the
four-block merge form with a typed block parameter, with either a boolean
condition or one of the six signed comparison operators. Multi-function
packages lower when every function is a single block whose body is an identity
parameter return, a constant return, a constant add return, a parameter
combined with a constant or a second parameter through `+`, `-`, or `*` (64-bit
`add`/`sub`/`imul`, so full-width Mako ints never truncate), a one- or
two-argument integer-constant call whose result is returned, or a
one-argument call combined with a constant through the same three operators.
Every other operator in these narrow shapes remains an explicit lowering error
(and falls through to the slot-per-value model below when the body fits it). A
body that
combines the results of two one-argument calls allocates a stack frame:
`push rbp; mov rbp, rsp; sub rsp, 16`, with the first result saved as a full
64-bit value at `[rbp - 8]`, rsp kept 16-byte aligned at every call, and
`leave; ret` as the epilogue. Subtraction reloads the saved first operand so
operand order is preserved. The `elf_two_call_exit`,
`elf_two_call_sub_exit`, and `elf_two_call_mul_exit` fixtures
(`let x = identity(a); let y = identity(b); return x op y`) exit 42 for all
three operators. Two-argument call pairs
(`let x = f(a, b); let y = g(c, d); return x op y`) reuse the same frame
through shared prologue and combine emitters; the `elf_two_call2_add_exit` and
`elf_two_call2_sub_exit` fixtures exit 42. Multi-function images also accept
callees in the verified three/four-block branch shapes — the same select and
compare bodies used at the image root — so a called function can branch on its
parameter; the `elf_call_branch_exit` fixture (`classify(value)` with
`if value > 1` called as `classify(2)`) exits 42. Branch arms may also return
call results: the three-block form (`if cond { return f(a) } return g(b)`)
lowers to an entry condition, a near `jcc` to the false arm whose displacement
is the true arm's length, and arms that are constant returns, zero-argument
calls, or one-/two-argument constant calls, freely mixed. The four-block merge
form (`if/else` converging through `IR_BLOCK_PARAM`) emits the same arms
without a trailing `ret`, a near `jmp` from the true arm over the false arm,
and a one-byte merge — both arms leave the value in `eax`, so the merge is
just `ret`. `IR_PARAM.aux` is the
locals-arena index, not the parameter position, so parameter registers are
assigned by emission order. The `elf_cfg_call_exit` fixture exits 7 with no
arguments and 42 with one (both arms execute), `elf_cfg_call2_exit` exercises
two-argument calls in arms (21 with no arguments, 42 with one),
`elf_cfg_merge_call_exit` covers the merge form (7/42 the same way), and
`typed_ir_cfg_call` now produces a 170-byte image that exits 1. Callees whose
arms contain calls with three or more arguments or owned values remain
explicit errors.

Straight-line scalar bodies with locals now lower through a slot-per-value
model: every SSA value gets one 64-bit stack slot in a sized frame
(`sub rsp, 8n` rounded to 16), parameters arrive in `rdi`/`rsi`, constants
materialize as immediates, and binaries operate directly from memory. Slots
are addressed with a disp8 ModRM form for the first fifteen values and a
disp32 form beyond them, so bodies grow to 127 values (frames past 127 bytes
use the `sub rsp, imm32` prologue — the imm8 form previously wrapped a
128-byte frame into `sub rsp, -128`; no fixture mixed that frame size with a
call, so nothing live was corrupted, but the encoding was a landmine);
larger functions remain explicit errors.
`+`,
`-`, and `*` use the 64-bit ALU forms; unary `-` and integer `^` lower to
the corresponding 64-bit `neg` and `not` instructions; checked `byte(int)`
conversions now preserve the 0..255 range and terminate on runtime overflow;
the unbound
`linux_syscall6(number, a1, ..., a6)` intrinsic now lowers directly to the
Linux x86-64 syscall ABI and is covered by `elf_syscall_exit` plus an arity
negative fixture. Explicit `unsafe_ptr_load8` and `unsafe_ptr_store8` intrinsics
now lower to direct byte memory operations; `elf_unsafe_memory` obtains one
page with `mmap`, stores 42, and loads it back. The unsafe names make the raw
pointer boundary visible in Mako source; pointer validity remains the caller's
responsibility. The gate pins the generated image and rejects invalid load
arity. `/` and `%` sign-extend the dividend
with `cqo` and divide by the operand slot with `idiv`, storing the quotient
from `rax` or the remainder from `rdx`, so signed Mako `int` division keeps
its truncation-toward-zero semantics (a zero divisor traps, matching the
stage-0 oracle). This is
the deliberately naive correctness-first model; a register allocator is a
later milestone. Root functions take at most one parameter (the entry ABI
defines only `argc` in `rdi`); callees take up to two. Bodies are capped at
127 values (the disp32 slot form described above). Bodies may also
call functions with up to fifteen arguments taken from earlier slots: the six
SysV integer-register tier (`rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`), assigned
by parameter position with a REX/ModRM table, then the remainder spilled to a
16-byte-padded outgoing area (`sub rsp, pad` before the register loads, each
stack argument stored at `[rsp + 8k]`, and the area reclaimed after the
call), so rsp stays 16-byte aligned at every `call`. Callees read stack
parameters from above the saved `rbp` and return address
(`[rbp + 16 + 8k]`) into their slots. Call results spill back into
slots, so call chains interleave freely with locals. The
`elf_scalar_locals_exit` fixture (`value * 2 + 10 - 4` across three locals)
exits 8 with no arguments and 10 with one; `elf_scalar_callee_exit`
(`combine(20, 9)` = `(a + b) * 3 - 5`) exits 82; `elf_scalar_call_exit` and
`elf_scalar_call2_exit` pass computed slot values as call arguments and exit
42 with no arguments, 43 with one. `elf_scalar_call3_exit` (three-parameter
`add3`) exits 43/44, and `elf_scalar_call6_exit` sums six parameters for 42 —
the disassembly shows all six argument registers loaded from slots.
`elf_scalar_div_exit` (`(value + 5) * 14 / 2`) exits 42 with no arguments and
49 with one; `elf_scalar_mod_exit` (`(value * 7 + 107) % 43`) exits 28/35.
`elf_scalar_call7_exit` sums seven parameters (`add7(10, 8, 6, 5, 4, 3,
value * 6)`) and exits 42/48 — the seventh argument crosses in both
directions through the stack; `elf_scalar_call9_exit` (`pick9` summing `a`,
`g`, `h`, `i`) exits 42/52 and reads all three stack-passed parameters from
`[rbp + 16]`, `[rbp + 24]`, and `[rbp + 32]`. Functions with sixteen or more
parameters remain an explicit error.

The four-block while-loop shape lowers through the same slot-per-value model.
A backedge (a jump targeting an earlier block) routes encoding to the loop
lowering before the branch shapes are tried. After one shared prologue, the
entry block emits its scalar prefix, copies the initializer slot into the
header parameter slot, and jumps forward into the header; the header skips
the block parameter (its slot is written by jumps), evaluates the condition
with the fused signed compare (or a boolean test) and a near `jcc` to the
exit; the body recomputes the loop local, copies the update slot into the
parameter slot, and jumps backward to the header with an immediately patched
negative displacement; the exit block returns through the usual scalar
return sequence. With more than one carried local the jumps copy through one
scratch slot per argument (reserved after the value slots, frame sized
accordingly): every argument lands in its own scratch slot before any
parameter slot is overwritten, so an argument still living in another
parameter's slot cannot be clobbered (parallel-copy safety). The value slots
plus scratch slots must fit the 127-slot frame cap — larger loops fail with
an explicit error until register allocation arrives. Call displacement edges
from every
segment are collected
with per-segment offsets and patched after layout as usual. The
`elf_loop_sum_exit` fixture (`total = total + 7` while `total < value * 5`,
returning `total + 1`) exits 8 with no arguments and 15 with one.
`elf_loop_bound_exit` (`i = 10` while `i < value`) never enters its body and
exits 10 either way; its condition ends in a bare identifier, which the
expression parser now handles like the stage-0 parser — struct literals are
suppressed in control-flow headers, so `i < value {` is a comparison, not a
struct literal (previously such conditions silently failed to attach and the
function stayed unlowered). `elf_loop_call_exit` (`let mut i = id(0)`, loop,
`return id(i)`) exits 6/9 and exercises call-displacement edges from the loop
entry and exit segments. `elf_loop_acc_exit` (`total = total + i * 2` and
`i = i + 1` while `i <= value + 2`) is the two-carried-local shape and exits
12/20; `elf_loop_acc_call_exit` adds a call in the initializer and exits 3/6.
`elf_loop_acc3_exit` carries three locals through disp32 slots (21 values
plus three scratch) and exits 32 either way; `elf_big_locals_exit` sums
eleven locals across a 24-value body and exits 67/68. Guarded loops lower
through the seven/eight-block shapes: `elf_loop_break_exit` (`if i == 12 {
break }` while `i < value * 10`) exits 12 either way, `elf_loop_break_acc_exit`
(`total = total + 2` before `if i > 5 { break }`) exits 4/6 — the break arm
copies the pre-guard update into the exit parameters before jumping, and the
header's false edge reaches them through the trampoline — and
`elf_loop_continue_exit` (`continue` skipping the accumulation once
`total > 4`) exits 6 either way. Loop bodies and conditions also call
integer-returning functions: `elf_loop_call_body_exit`
(`total = total + double(i)` while `i < bound(value)`) exits 20/90,
`elf_loop_call_break_exit` (`i = step(i)` before a break guard) exits 6/12,
and `elf_loop_call_continue_exit` (the same call before a continue guard)
exits 15/26. Nested if/else assignment blocks lower through the merge-if
shape: `elf_loop_merge_exit` (`total = total + i` / `total = total + 10` in
the arms) exits 25/47, `elf_loop_merge2_exit` merges two locals and exits
64/82, and `elf_loop_merge_post_exit` (empty else, then a post-if
`total = total + 1` overriding the merge on the backedge) exits 6/21.
Loop bodies may also declare scalar temporaries (`let step = double(i)`):
each is a fresh slot-per-iteration local resolved like any straight-line
local, never a carried one — `elf_loop_let_exit` exits 12/56 and
`elf_loop_let_break_exit` (a body let ahead of a break guard) exits 12
either way. Assigning to a body let, or declaring an owned one, stays
unsupported. Functions may also chain several plain loops: each loop gets its
own header block parameters over the carried union (locals a loop does not
update pass through as identity arguments), and a junction block carries the
final parameters into the next header — `elf_loop_seq2_exit` (two loops over
one counter) exits 5/10, `elf_loop_seq3_exit` (a shared accumulator) exits
5/14, and `elf_loop_seq4_exit` (three loops, two locals) exits 250/243.
The last loop of a chain may also carry a break/continue guard: the plain
prefix runs through the chain lowering and the guarded group attaches at the
final junction — `elf_loop_seq_break_exit` (a plain accumulator loop, then a
guarded one) exits 18/22 and `elf_loop_seq_continue_exit` exits 12/18.
Guards in earlier loops and merge-ifs in chains remain unsupported. One
merge-if may also lead the function at function scope: its merge block
carries one parameter per arm-assigned local and doubles as the first loop's
entry block — `elf_pre_if_exit` (`total = 5` or `10`, then a sum loop) exits
13/20, `elf_pre_if_chain_exit` (two merged locals, two loops) exits 17/19,
and `elf_pre_if_guard_exit` (merge-if plus a guarded loop) exits 11/13.
Requesting an output image for a source whose
functions were skipped by typed-IR lowering is now a hard error instead of
emitting an image for whatever functions remained.

The first owned-values slice lowers string literals, `print`, `exit`, and
`len`. The typed IR gained a `string_literals` pool with a
`constant_string_index` side table (built at the single constant-append site),
and three new ops: `IR_PRINT`, `IR_EXIT`, and `IR_LEN`. A string constant
lowers to a pointer to an inline `{ len: u64, bytes..., 0x0A }` struct emitted
directly into the code stream behind a near `jmp` and referenced with a
RIP-relative `lea` — no relocations and no ELF layout changes. `print(s)`
emits a `write(1, data, len)` syscall followed by a one-byte newline write,
matching the stage-0 `print` which appends one newline per call; `print(int)`
runs an inline itoa: the newline is pre-stored at `[rsp - 8]` in the red zone
and digits are built backward below it with unsigned division by ten (so
INT_MIN survives the negation), followed by the same two-write sequence.
`exit(code)`
emits the `exit` syscall (60); `len(s)` loads the length field from the
struct. `IR_DROP` of a string value is a no-op today: every backend string is
literal-backed (static, non-owned), so there is nothing to free — this must
become a real ownership check (or an actual free) the moment an allocating
string source exists. Prints and exits are accepted in straight-line
single-block bodies and as direct children of a plain while body; guards,
merge-ifs, sequential chains, and leading merge-ifs keep them unsupported.
Body prints emit after the body's value instructions in source order — every
argument reads the SSA value current at its source point, so the append
position is transparent, and an `exit`'s syscall never returns, making the
fallthrough unreachable. A body print may only read carried locals no
earlier body statement updates: anything else would resolve to the stale
header parameter (the same rule right-hand side reads already follow), and
`bad_loop_print_stale` pins the rejection. Owned (string) arguments are
consumed by an explicit `IR_DROP` after the print so every path through the
body defines and uses the value exactly once; the verifier treats a
constant's redefinition through the backedge as fresh re-materialization
(constants own nothing), never a double definition. The `bad_print_loop`,
`bad_print_if`, and `bad_loop_print_guard` fixtures pin that requesting an
image for an unsupported placement fails
with the cannot-lower diagnostic and leaves no output file. The `elf_hello_exit` fixture
prints `hello`, ` `, and `world` across three calls and exits 42; the gate
pins its image hash, compares stage-0 driver stdout byte-for-byte, and
executes the image on Linux x86-64 checking stdout and exit status. The
`elf_print_int_exit` fixture prints `42`, `-7`, `0`, `1000000000000`,
INT_MIN, and INT_MAX and exits 7, with the same hash, differential-stdout,
and Linux-execution coverage; `bad_print_type` pins the typer's rejection of
`print(float)`. The `elf_loop_print_exit` fixture interleaves integer prints,
a string print, and two carried locals across four iterations of a plain
while body and exits 60, with identical coverage. The
`typed_ir_owned_string_drop` fixture now produces a 189-byte image that exits
1 — its string local lowers, and its drop is the documented literal no-op.
String concatenation (needs an
allocator) remains a future slice.

Beyond shape pins and image hashes, the gate runs a stage-0/stage-1
differential execution pass: the current compiler builds every executable
fixture with a driver that feeds `argc` exactly like the stage-1 entry bridge,
and both binaries must agree on exit status. This anchors the expected values
to the oracle compiler's semantics rather than to the stage-1 lowering alone.
(Stage 0 ignores a `-> int` main's return value, so the driver renames any
fixture `main` and wraps the call in `exit(...)`; `argc()` needs an explicit
`int(...)` conversion under stage-0 typing.) Bodies that mix one- and
two-argument call pairs, and bodies combining three call results, lower
through the same slot-per-value model — call results spill to slots in order
and every argument register reloads from its slot, so arity never interacts:
`elf_mixed_call_exit` (`add_one(value) + add(20, 20)`) exits 42/43,
`elf_mixed_call2_exit` (`add(19, 2) * add_one(value)`, two-argument call
first) exits 42/63, and `elf_three_call_exit`
(`id(value + 9) + id(20) + id(12)`) exits 42/43. The root is the unique
function no call site references; bodies are laid out in arena order after the
entry bridge and every `call rel32` displacement is patched once offsets are
known, so encoding order never affects the bytes. The `typed_ir_call` fixture
(identity + caller) produces a 151-byte image that exits 1 on Linux x86-64,
and the `elf_call_add_exit` fixture (`add_one` + `add_one(40) + 1`) produces a
164-byte image that exits 42. The `elf_call2_add_exit` fixture (`add(a, b)` +
`add(40, 2)`) produces a 160-byte image that also exits 42, exercising
positional `edi`/`esi` argument passing. The `elf_sub2_exit`,
`elf_mul_param_exit`, and `elf_call_sub_exit` fixtures cover subtraction and
multiplication in the parameter-parameter, parameter-constant, and
call-result positions; all exit 42, and a reversed `sub(8, 50)` probe exits
214 (−42 modulo 256), proving operand order is preserved. The IR successor
order fixes which arm the conditional jump selects. IR
shapes outside the supported subset are explicit lowering errors: requesting
an output artifact for one fails the compile with a source-located diagnostic
and no file, while frontend-only invocations (no output argument) stay quiet
so the stage-1 binary can still gate frontend fixtures it cannot lower yet. The
`elf_select_exit` fixture produces a 153-byte image that exits 42 when
executed on Linux x86-64, and the `elf_compare_exit` fixture produces a
158-byte image whose `argc > 1` condition makes both arms reachable from the
process command line: it exits 7 with no arguments and 42 with one or more.
The gate pins the exact SHA-256 of every generated image (so immediate-value
regressions are caught on any host) and additionally executes the images —
both compare arms included — when running on a Linux x86-64 host. General instruction
selection, register allocation, stack frames, and multi-function images remain
future backend milestones.

### Entry argv boundary

The self-hosted x86-64 backend now exposes native `argc()`, plus
`unsafe_argv_ptr(index)`,
`unsafe_argv_len(index)`, `unsafe_argv_byte(index, offset)`, and
`unsafe_argv_copy(index, []byte)` as explicit
entry-only unsafe primitives. They read
the original Linux process `argv` vector directly from the entry frame
and return the selected pointer or its NUL-terminated length
without a C or Rust helper. The vector *begins* at `rbp + 24`, so the base is
taken with `lea`; loading `[rbp + 24]` yields `argv[0]` and indexes inside the
first argument string. The `elf_unsafe_argv_ptr` and
`elf_unsafe_argv_len`, `elf_unsafe_argv_byte`, and `elf_unsafe_argv_copy`
fixtures pin their generated ELF images, including arity-negative checks.
Darwin/arm64 hosts validate ELF format and image hashes and defer execution;
the `selfhost-linux` CI job runs the same gate on Linux x86-64 and executes
every generated image.

`unsafe_bytes_to_string([]byte)` now allocates the native `{length, bytes}`
string representation with Mako-emitted `mmap` and `rep movsb`, so copied
argv data can enter the existing string print/write path. The IR identifies
these intrinsic results as owned and emits `munmap` on their drop paths;
literal strings remain static and are never unmapped.
Native `arg_get(index)` now uses the same owned allocation path directly from
the process argv vector; the stage-0 compatibility frontend remains separate
until the final Rust bootstrap removal.
`unsafe_close(fd)` completes the direct file-descriptor lifecycle for native
open/read/write sequences.
`unsafe_read_file(path)` now performs exact-size Linux `fstat`/`read` into an
owned Mako string and releases both the descriptor and stat scratch storage.
`unsafe_write_file(path, contents)` completes the direct write path with
`openat`, truncating creation, retrying `write` until complete, and `close`.
`unsafe_write_all(fd, ptr, len)` now supplies the retry loop for callers that
need complete writes; it returns zero on success and preserves negative syscall
errors.
`unsafe_read_all(fd, ptr, len)` symmetrically handles short reads and EOF,
returning the number of bytes received or a negative syscall error.
`unsafe_open_string(path)` now opens the existing inline Mako string form
directly, completing string-based descriptor acquisition.

### Byte slices and the string object layout

`make([]byte, n)`, indexing, `len`, and capacity-aware `append` lower to a
three-word header (`[length, capacity, data pointer]`) followed by an inline
payload, allocated with Mako-emitted `mmap`. A full `append` reallocates at
`(capacity | 1) * 2`, copies with `rep movsb`, and unmaps the old block.
`make` parses as a chain — a base node carrying the type, then one node per
argument — and only the terminal node is the construction; lowering the prefix
as well allocated a second buffer per `make` that nothing read or freed.
The `elf_make_byte_len`, `elf_make_byte_index`, `elf_make_byte_append`,
`elf_make_byte_grow`, and `elf_make_byte_grow_zero` fixtures cover the
in-place and reallocating paths, and `elf_open_byte_slice`,
`elf_read_byte_slice`, and `elf_write_byte_slice` carry slices into syscalls.

Every string object — inline literal and heap-allocated alike — is
`{length: u64, bytes..., 0x00, 0x0A}`. The NUL terminates the payload so a
string can be handed straight to a path-taking syscall, and the newline one
byte later is what `print` writes after the payload. Allocating string
producers (`unsafe_bytes_to_string`, `arg_get`, `unsafe_read_file`) therefore
reserve `length + 10` bytes and write both terminators. `unsafe_read_file`
fails closed: a failed open or read clamps the stored length to zero rather
than recording a negative count that later readers would see as an unsigned
multi-exabyte string.

### First shape-independent encoder

`x86_lower_returning_cfg` encodes any function whose every block ends in
`IR_RETURN` or `IR_BRANCH` — an arbitrarily nested `if`/`else` tree in which
every path returns. It is the first backend lowering that is general in its
control-flow shape rather than matched against a block count.

It works because no path rejoins: with no merge blocks there are no block
parameters and no jumps, so the slot-per-value model needs nothing beyond
branch displacements. Each block is emitted independently in arena order, its
offset recorded, and every displacement patched once all offsets are known, so
nesting depth is not a shape the encoder recognizes. Each branch emits
`jcc false_target` then `jmp true_target` instead of relying on one arm falling
through; that costs five bytes per branch and makes correctness independent of
block layout order. Comparisons still fuse into the `jcc` because the slot
model has no compare-to-slot form.

It is wired as the **last** dispatch case, so every existing shape keeps its
own encoder and every pinned fixture image stays byte-identical.

`x86_returning_cfg_smoke_test` hand-builds the five-block IR for
`if v > 10 { if v > 20 { return 3 } return 2 } return 1` and pins both the
accepted encoding and the block-parameter, escaping-successor, and
non-branch-terminator rejections.

### General IR CFG construction

`ir_lower_returning_if_tree` is the matching frontend half: it lowers a body
that is an arbitrarily nested tree of `if`/`else` and `return` by walking the
statement tree, not by testing statement counts. It is tried only after the
matched shapes decline, so their IR — and every pinned fixture image — is
unchanged.

The walk is a FIFO over statement lists. Each list becomes exactly one block,
and lists are discovered in the order their blocks are allocated, so a list's
index is its block index and blocks come out in arena order. Statements
following an `if` at the same level *are* that `if`'s else path, which is why a
guard clause and an explicit `else` fall out of one rule rather than two
shapes. Expressions are lowered by a single forward pass over the expression
arena filtered to the block's token range: the Pratt parser builds the arena
bottom-up, so arena order is already a topological order and an operand is
always lowered before the parent that consumes it — that is what replaces a
recursive walk in a language without recursion here.

Scalar `let` bindings are supported anywhere in the tree. A binding's
initializer lowers in the block that declares it and the binding aliases the
resulting SSA value; because that definition dominates every block reachable
from there, no block parameter is needed. An arm-local binding cannot leak into
a sibling arm: the resolver has already chosen the binding for each reference's
scope, and an identifier whose binding holds no value yet fails closed instead
of aliasing value zero.

Two constraints keep the slice honest. Every path must return, so nothing
rejoins; and only scalar literals, identifier references, unary and binary
operators are accepted, so a construct the path does not model fails instead of
lowering silently. Calls and loops inside the tree remain the next extension.

Coverage: `elf_nested_if_exit` (five blocks), `elf_nested_if_deep_exit` (four
levels, nine blocks), `elf_nested_else_exit` (nested `else` in both arms, seven
blocks), and `elf_nested_let_exit` (entry-block bindings read from both arms, an
arm-local binding, and a binding on the fallthrough path, returning negative
values on two of six inputs) pin their IR shapes and images, and all four run in
the stage-0/stage-1 execution differential. The `ir_lower_returning_if_tree`
path is capped at thirty-two blocks per function to stay inside the verifier's
dominance bitmask limit.

### Global const references

References to a global `const` whose initializer is one literal are now typed
and lowered. The root cause was in the typer, not the IR: an identifier that
resolved to a const item stayed `SEM_UNKNOWN`, and every operator built on it
stayed unknown too, so the reference read downstream as an unsupported
expression. `semantic_const_reference_type` classifies the initializer literal
(int, float, string, bool) and `ir_const_reference_value` evaluates the integer
form into the constant pool, both rejecting a computed initializer such as
`const A = B + 1` so it stays deferred rather than guessed.

Fixing the typer is what made this pay off on real code: coverage over
`compiler/*.mko` moved from 19 lowered functions to **22** — the first movement
from any of the general-path work — because long `if kind == TK_X { return ... }`
chains over token and expression constants are common in the lexer and typer.
`elf_nested_const_exit` pins the shape and image and runs in the execution
differential, covering a negative const (`const OFFSET = -3`) as well.

### Calls in the general path

Calls to declared functions lower inside the branch tree. Arguments hang off
each call-chain link's right edge, innermost last, so the walk collects them in
reverse and the call site records them in source order; `IR_CALL.aux` indexes
the call site as everywhere else, and the backend needed no change because
`x86_emit_scalar_segment` already emits calls and reports their displacement
fields, which `x86_lower_returning_cfg` was already forwarding.

One ordering detail mattered: a callee name carries no value and therefore no
semantic type, so the call-target skip has to run *before* the
known-type requirement. With the checks in the other order every call failed
with "CFG lowering needs a known type for every expression".

`elf_nested_call_exit` covers a call bound by `let`, one- and two-argument calls
in nested arms, and matches the oracle on six inputs.

This moved coverage over `compiler/*.mko` by **zero** — still 22 lowered. Three
control-flow increments in a row (`let`, calls) reached no real compiler
function, and the reason is now measured: **only 71 of 243 functions have
scalar-only signatures**; the other 172 take or return a struct, a `[]T`, or a
map. Struct layout and `[]T` are therefore the actual gate, and they have been
reordered to the front of the roadmap ahead of any further control-flow work.

The *shape-matched* path still fails the whole compile on a global const
(`typed if condition did not lower to an SSA value`) rather than skipping the
function, because materializing a constant there has to be routed into the
right arm bucket by the shape-specific instruction routing. That is fixed by
migrating those shapes onto the general path, not by patching the routing.
Single-file compilation also cannot resolve calls across `compiler/*.mko`,
which is why `x86_64.mko` fails verification outright.

### Slices beyond one byte

The slice emitters take an element stride (`x86_slice_element_size`: 1 for
`[]byte`, 8 for `[]int`). The header keeps length and capacity in *elements*, so
only the payload allocation and the addressing scale: `make` allocates
`length * stride + 24`, the bounds check still compares the index against the
element count and scaling happens after it, indexing uses
`lea rax, [rax + rcx*8]` or a plain `add` for bytes, and `munmap` scales the
capacity. `[]byte` takes the same branches as before, so its images are
byte-identical.

`append` also had to stop being typed as `[]byte`: the typer hard-coded
`append` to `SEM_ARRAY_BYTE`, so indexing the result of an `[]int` append
produced a byte and `element + int` was rejected as arithmetic on unrelated
operands. It now takes the slice type from its first argument, and a
non-slice first argument stays unknown rather than defaulting.

`make`, indexing, `len`, and `append` all work for `[]int`.
`x86_emit_clone_slice` implements `IR_CLONE` for a slice: allocate
`capacity * stride + 24`, copy the header's length and capacity, point the new
header at its own payload, and `rep movsb` the elements. `SEM_ARRAY_INT` is in
`ir_type_is_owned` while `SEM_ARRAY_BYTE` is not, which is why only `[]int`
reached this path at all. The source header, length and capacity live in
r12/r13/r14 across the `mmap`, and r15 holds the new header across the copy.
`elf_int_slice_exit` and `elf_int_append_exit` both match the oracle.

### A stage-0 use-after-move on slices

Found while building the append fixture, and worth recording because stage 0 is
the production compiler and the oracle for every differential here:

```mako
let xs: []int = make([]int, 2)
let grown = append(xs, 7)
print_int(len(grown) + xs[0])   // reads xs after append consumed it
```

Stage 0 accepts this at `check` time and then **segfaults at run time** — 10 of
10 runs on the minimal case, and non-deterministically (12 or 139 on identical
input) in a larger one. `append` moves its slice argument, as
`examples/testing/append_move_test.mko` shows with the sanctioned
`xs = append_one(xs, i)` reassignment pattern; reading the moved-from binding
afterwards should be an ownership diagnostic, not a crash.

Two things follow. The differential fixture here deliberately avoids the
pattern, because a fixture anchored to a crashing oracle proves nothing. And the
self-hosted IR does not share the bug: it clones on transfer, so the original
stays valid — a documented, intentional difference from stage 0's move, not a
divergence introduced by this work.

### Ownership operations in the general path

The general path now tracks owned values and emits their lifetime operations, so
`string` and `[]T` parameters and locals are accepted rather than refused.

Each block inherits its parent's set of live owned values, which is what makes
dominance fall out of the tree shape: anything in scope at a block was defined by
a block that dominates it. At every return the path emits an `IR_MOVE` when the
returned value is owned — so the return consumes a fresh owner — and an
`IR_DROP` for every other owned value still live on that path. That gives
exactly-once consumption per path, which is precisely what the ownership
verifier checks, so a mistake here fails the gate rather than corrupting memory.

`IR_CLONE` on call arguments completes the group. Passing an owned binding to a
call consumes it, so the callee receives a clone and the caller's original stays
live for its own drop; without this the value would be consumed twice on one
path and the verifier would reject the function. `elf_owned_pass_exit` chains it
— `main` allocates, `describe` takes the slice and passes it on to `total`, and
both branch arms make the onward call — and traces **three `mmap`s and three
`munmap`s on both arms**, matching the make plus two clones with each released
exactly once.

`SEM_ARRAY_BYTE` is deliberately still refused by this path: it is a one-slot
allocation that `ir_type_is_owned` does not classify as owned, so the path
cannot tell whether to drop it and declines instead of guessing.

The backend needed two small pieces. `IR_MOVE` had **no case at all** and is now
a slot copy — a move transfers ownership without touching the allocation, and
the source is dead by the verifier's linearity rule rather than by anything
emitted. And the parameter check required `SEM_INT`; every supported parameter
type occupies one slot and arrives identically, so it now accepts any one-slot
type via `x86_one_slot_type`.

Two shape encoders also stopped masking the general path: a three- or four-block
function whose narrow branch encoders both decline now falls through to
`x86_lower_returning_cfg` instead of reporting their shape complaint, which is
not the reason such a function cannot be encoded.

`elf_owned_param_exit` passes an owned `[]int` to a second function and matches
the oracle. Its allocation trace on Linux is balanced — two 48-byte `mmap`s, two
`munmap`s. The accounting spans both paths: the shape-matched caller allocates
the slice and clones it to pass it on, and the general path supplies the
callee's drop of its owned parameter. Each allocation is released exactly once.
`typed_ir_owned_string_move` and `typed_ir_cfg_hold_call_argument` now lower to
images as well as IR summaries, which is why their gate expectations gained an
`elf_bytes` line.

### Register discipline for emitted syscalls

Syscall argument registers are `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9`. Two
constraints are easy to violate and produce code that passes image-hash pins
while faulting at run time:

- Constants must be written straight into the target register. Staging them
  through `rax` overwrites the syscall number the caller already loaded.
- `syscall` itself destroys `rax`, `rcx`, and `r11`, and the six argument
  registers are in play, so any value that must survive a syscall belongs in
  `r12`–`r15`.

Positions 4/5/6 (`r10`/`r8`/`r9`) also need `REX.B` (`0x49`). Position 4 shares
ModRM `0xC2` with `rdx`, so an omitted `REX.B` silently writes `rdx` and leaves
the fourth argument uninitialized — which for `mmap` means a corrupted
protection value and unset flags.

## Verification command

Run `./scripts/native-compiler-test.sh` before considering a native-compiler
increment complete. It executes the Rust compiler unit suite, the stage-1
self-host frontend and negative-fixture gate, the owned-slice move regression,
an instrumented reallocation workload (Guard Malloc on macOS, executable ASan
elsewhere), an instrumented stage-1 run over the rich literal fixture on
macOS, and differential C/native execution for the currently supported native
fixtures. New backend features must add a positive differential fixture and,
when applicable, a negative diagnostic or memory-safety fixture before their
implementation is treated as complete.

**A green gate on macOS is not evidence that generated code runs.** On any
non-Linux-x86-64 host the backend gate checks ELF structure and pins image
hashes, then skips execution — so a miscompiled register or syscall sequence
reproduces byte-for-byte and passes. Backend changes must be validated by the
`selfhost-linux` CI job, or by running `./scripts/selfhost-gate.sh` on a Linux
x86-64 host, before they are called done. Image hashes are descriptive; the
stage-0-anchored exit statuses in the exec-differential specs are the
prescriptive part and must never be regenerated to match new output.
