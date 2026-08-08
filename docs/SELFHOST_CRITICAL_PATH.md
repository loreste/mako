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
| signatures | 321 |
| ir_functions lowered | **161** (floor in `selfhost-gate.sh`) |
| ir_skipped | ~164 |

## Highest-leverage order (after constructible returns)

1. ~~Struct-returning calls / returns (string + `[]int` fields)~~ done for constructible shapes  
2. ~~Array literals (`IR_ARRAY` → make/append)~~ done for non-empty and annotated empty `[]T`  
3. ~~Loop-carried owned slices~~ sole and dual (scalar + one owned slice)  
4. ~~Owned field → call without double-drop~~ pending-drop cancel on consume  
5. ~~`len(mk())` mid-expression owned call~~ IR only; x86 still rejects string-returning mid-calls  
6. ~~String `==` / `!=` (IR + x86 emit + temp drops)~~ pin `elf_str_eq_exit`  
7. ~~`if/else` followed only by `continue`/`break`~~ arm-dup + pin `elf_if_else_continue_exit`  
8. ~~`if/else` then more statements~~ arm-dup post into both arms (no join/phi); pin `elf_if_else_merge_exit`  
9. **Remaining untyped / cascade calls** (~46+35)  
10. ~~Mid-expression owned call as arg/field~~ sealed; pin `elf_nested_owned_call_exit`  
11. **Nested constructible fields** (e.g. `X86Body { code: X86Code }`) — layout still refuses; blocks x86 body helpers  
12. **`[]Token` / constructible LexResult** — deferred: stage-0 seed backends choke on `[]Token`  
13. **Register allocation**  
14. **Runtime pieces still on C** → Mako syscalls  
15. **Fixed point** stage 1→2→3 + purity audit  

## Safety invariants while growing the backend

- Unsupported → fail closed, never silent fallback to C/Cranelift  
- Owned values: clone-on-pass for strings/slices; struct params borrow  
- Element of `[]Struct` is `IR_ELEM_ADDR` / fused field, never wrong-width `IR_INDEX`  
- Pin ELF hashes; execute on Linux; soaks for mmap/munmap balance  

## Do not

- Port features by growing Rust product surface  
- Update pins without a byte-level reason  
- Claim full self-host before fixed-point + purity audit  
