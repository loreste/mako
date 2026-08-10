# Self-host critical path (working notes)

**Branch:** `selfhosted` only. Do not merge to `main` (production users).  
**Canon plan:** [MAKO_ONLY_SELFHOST_PLAN.md](MAKO_ONLY_SELFHOST_PLAN.md)  
**Rule:** product logic is Mako. Machine code is emitted **directly in Mako** (`x86_64.mko` + `elf64.mko`). No C/Rust/LLVM/assembler/linker in the final stage-1+ path.

## Seed vs product

| Role | What |
|---|---|
| Stage 0 today | Rust `mako` builds stage 1 from `compiler/*.mko` (temporary seed) |
| Stage 1 | Mako compiler → raw x86-64 + ELF bytes |
| Goal | Stage 1 builds stage 2; stage 2 ≡ stage 3; seed only starts a clean machine |

## Measure first

```bash
MAKO_RUNTIME=$PWD/runtime ./target/release/mako build compiler/main.mko -o /tmp/s1
pkg=compiler/lexer.mko,compiler/parser.mko,... # gate package list
/tmp/s1 "$pkg" /dev/null reasons | tr '|' '\n' | sed 's/.*: //' | sort | uniq -c | sort -rn
```

Publish `ir_functions / signatures` from tooling, not prose.

## Measured package floor (re-check after each increment)

| Metric | Value |
|---|---|
| signatures | 339 (14 helper functions added by the nested-fields/sliceability/dynamic-make increments) |
| ir_functions lowered | **175** (floor in `selfhost-gate.sh`) |
| ir_skipped | ~162 |

## Highest-leverage order (after constructible returns)

1. ~~Struct-returning calls / returns (string + `[]int` fields)~~ done for constructible shapes  
2. ~~Array literals (`IR_ARRAY` → make/append)~~ done for non-empty and annotated empty `[]T`; one-argument dynamic slice lengths now lower through `IR_MAKE` and direct x86-64 emission  
3. ~~Loop-carried owned slices~~ sole and dual (scalar + one owned slice)  
4. ~~Owned field → call without double-drop~~ pending-drop cancel on consume  
5. ~~`len(mk())` mid-expression owned call~~ IR only; x86 still rejects string-returning mid-calls  
6. ~~String `==` / `!=` (IR + x86 emit + temp drops)~~ pin `elf_str_eq_exit`  
7. ~~`if/else` followed only by `continue`/`break`~~ arm-dup + pin `elf_if_else_continue_exit`  
8. ~~`if/else` then more statements~~ arm-dup post into both arms (no join/phi); pin `elf_if_else_merge_exit`  
9. **Remaining untyped / cascade calls** (~46+35)  
10. ~~Mid-expression owned call as arg/field~~ sealed; pin `elf_nested_owned_call_exit`  
11. ~~Nested constructible fields~~ done: flattening layout, address-form field reads, stride-override drop masks; pins `elf_nested_*` (9 fixtures incl. 2 soaks) + `nested_pkg_*` + 5 `bad_nested_*` refusals  
12. **`[]Struct` with owned element fields** — flat owned fields are supported:
string, `[]int`, and `[]byte` fields clone independently across literals, bindings,
and multi-element growth (`elf_owned_struct_slice_*`). POD literals and append
growth use the stride-wide copy path (`elf_struct_slice_literal_exit`,
`elf_struct_slice_append_exit`). Nested struct slices and `[]string` fields remain
refused. `LexResult { tokens: []Token }` with POD `Token` is constructible and
pinned (`elf_nested_lex_*`).
13. **Register allocation**  
14. **Runtime pieces still on C** → Mako syscalls  
15. **Fixed point** stage 1→2→3 + purity audit  

## Nested constructible fields (increment 11)

**Representation.** A field whose type is an earlier *constructible* struct is
flattened: the inner struct's slots are laid out inline in the outer block, so
a nested value stays one allocation and the drop mask covers inner owned slots
directly — no second block, no recursive drop. A field of `[]Struct` (element
must be *eligible*) is one pointer slot owning the slice block. Both shapes are
constructible but never slice-eligible. Resolution only looks backwards in
declaration order, so forward references, self-references and cycles never
resolve and fail closed.

**Lowering.** A nested field initialiser must be a literal, a call result, or
a binding of the inner struct (anything else is refused). Construction reads
each inner slot out of the source (`IR_FIELD`), clones owned slots so the outer
block owns independent copies, and drops a temporary source once, after its
last slot is read. A binding source keeps its scope-exit drop; aliasing one
binding into two nested fields is sound because both sides clone. A nested
field *read* (`body.code.bytes`) lowers to an address-form `IR_FIELD`
(`IR_FIELD_ADDR_FLAG` in aux: block + 8·slot, no load) that only a chained
field read may consume — `let c = body.code` and other moves of a nested field
out of its parent are refused, never a double-free.

**Drop masks.** `IR_DROP.aux` keeps 2 bits per slot (string/`[]int`/`[]byte`);
a `[]Struct` slot adds a stride override above bit 30 (up to three 10-bit
entries: slot index + stride in slots). Structs past the bounds (15 slots, 3
struct-slice fields) stay inconstructible.

**Pre-scan bug ("no owning block").** The 4fa1de4 soft-skips (unlowered
simple-if/merge conditions) did `continue` after the matched shape had already
appended the function's `IR_PARAM`s (and possibly constants/call sites) to the
shared arenas, leaving instructions no block owned and constants no instruction
referenced. Any function with parameters hitting that skip failed package
verification. Fix: arena marks are taken at function start and every soft-skip
rewinds all six arenas (`ir_rewind_*` helpers). Proven by the `nested_pkg_*`
package fixture and the full-package build.

**Adjacent memory fixes required by the increment** (each changes a pinned
image; byte-level reasons):
- Matched-shape scope-exit drops carried `aux: -1`, so a struct value dropped
  on those paths freed its block but leaked every owned field. All such sites
  now use `ir_drop_aux`. Grows elf-nested-owned-call 501→557 (tag freed),
  elf-flat-body 733→833 (bytes+error freed), elf-struct-field-call 1218→1318,
  ss-ret-string 483→539.
- Slice-typed borrow drops emitted an unconditional munmap: reading an owned
  slice field (`len(c.bytes)`) freed the field out from under its struct, and
  the struct's own drop freed it again. The backend now emits a slice drop
  only for owned producers (`x86_slice_value_owned`; `IR_FIELD` results are
  borrows). Combined with the mask fix: ss-ret-code 653→660 (static sites stay
  4 mmap / 3 munmap, but the double-free is gone).
- Pre-existing drift, not this increment: 76f04fb tightened call-site
  clone/consume (intrinsics borrow strings; temps handed to consuming callees
  are not cloned) without re-pinning elf-string-clone (1087→814) and
  elf-str-byte-at (688→436). Bisected: pins reproduce at 3dc7a05 and drift at
  76f04fb. elf-string-clone's strace balance moves 5→3 accordingly.
- More pre-existing drift found while re-running the full gate on this host
  (each verified byte-identical between a pristine-HEAD stage 1 and this
  increment's stage 1, so none of it is caused by the nested-fields work):
  literals.mko `typed 53`→`54` (8263879's array-literal typing; bisected),
  typed_ir_owned_drop / typed_ir_owned_move (gained `elf_bytes` lines and
  `typed 7`→`8`), typed_ir_owned_string_array (`typed 5`→`6`, one more
  instruction), and the elf_bytes substrings/sha256 of elf_unsafe_open_string
  (225→237), elf_unsafe_write_file (364→372) and elf_unsafe_read_file
  (638→642). And bad_loop_print_guard.mko lowers since d8fa222 (arm-dup
  carries trailing statements into both arms) — renamed to
  elf_loop_guard_print_exit and pinned positively, the same treatment the gate
  comment already records for two earlier loop-print fixtures.

**Adversarial boundary (probed after the increment landed).** Cases outside
the fixture set, and how stage 1 behaves — all fail closed, none crash, none
emit a partial image:

- Whole-value rebinding of a nested struct inside a loop: refused at lowering
  (`bad_nested_loop_rebind`).
- Assignment through a nested field path (`o.inner.n = 9`): refused at
  lowering (`bad_nested_field_assign`).
- A `[]Struct` field initialised from a `make`-produced value — binding or
  direct call — trips the ownership verifier ("owned value is not consumed
  exactly once"): `bad_nested_slice_binding`, `bad_nested_slice_make`.
  Append-grown bindings work (`elf_nested_lex_exit`).
- One struct literal mixing a nested-struct field *and* a `[]Struct` field
  trips the verifier ("uses consumed owned value"): `bad_nested_mixed`.
- Rebinding a binding after it served as a nested-field source works and is
  pinned positively (`elf_nested_source_rebind_exit`, oracle exit 6); passing
  a nested struct by value works (`elf_nested_param_exit`, oracle exit 8).
- Single-line `struct S { a: int, b: string }` declarations parse into a
  different (broken) shape than the canonical multi-line form and die at the
  verifier — a parser gap to fix or reject early, tracked here so nobody
  reads the verifier failure as an ownership bug.

All seven adversarial outcomes above are pinned in `selfhost-gate.sh`: the
five refusals joined the `bad_nested_*` reason loop, and the two passing
shapes carry cap pins (summary + sha256 + stage-0-oracle exit, executed on
Linux x86-64).

**Counts.** signatures 325→337 (five `ir_rewind_*` helpers, four decl-layout
helpers, `x86_emit_field_addr`, `x86_slice_value_owned`,
`struct_layout_nested_smoke_test`), ir_functions 161→175, ir_skipped 164→162.
Floor pin raised to 175.

## Safety invariants while growing the backend

## Owned struct-slice construction boundary

The current self-host slice subset can allocate a constructible non-POD struct
slice, bind an element as a borrow, and read scalar fields. Its first owned-field
case is limited to struct literals or existing bindings with string, `[]int`,
and `[]byte` fields: each
string is cloned into the slice and released before the slice mapping is unmapped.
Owned slice fields remain refused. A shallow copy of an
owned string or slice field would leave two elements pointing at one allocation
and would turn the following drop into a use-after-free or double-free.

The next implementation must preserve the existing inline element layout and
introduce an explicit ownership operation with these rules:

1. Lower an owned struct element into a fresh struct value by cloning every
   owned field, using the existing struct drop mask as metadata.
2. Insert the cloned value into the slice with a stride-wide copy.
3. Drop the temporary struct block after insertion; the slice owns only its
   independent field allocations.
4. Drop every owned field of every live slice element before unmapping the slice
   payload, including zero/null fields from `make`.
5. Refuse nested owned-slice elements, unsupported field kinds, partial field
   initialization, and any representation that cannot prove exactly-once
   cleanup.

The IR should make the clone and source-drop visible rather than hiding them in
`IR_ARRAY`. The backend must reject an owned-element append if the metadata is
missing or exceeds the bounded drop-mask representation.

- Unsupported → fail closed, never silent fallback to C/Cranelift  
- Owned values: clone-on-pass for strings/slices; struct params borrow  
- Element of `[]Struct` is `IR_ELEM_ADDR` / fused field, never wrong-width `IR_INDEX`  
- Pin ELF hashes; execute on Linux; soaks for mmap/munmap balance
- `arena` is a hard keyword in the self-hosted lexer (stage 0 treats it as a
  soft one): naming a parameter `arena` compiles under stage 0 but stage 1
  reports "expected parameter name" on its own sources  

## Do not

- Port features by growing Rust product surface  
- Update pins without a byte-level reason  
- Claim full self-host before fixed-point + purity audit  
