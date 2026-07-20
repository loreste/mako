# Self-hosted native compiler

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
leak a value from one sibling arm into another: operands must be defined in the
same block or in the entry block for the current bootstrap CFG subset. Multiple
block parameters and general dominator construction remain future extensions.

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
