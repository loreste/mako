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
| ir_functions lowered | **113** (floor in `selfhost-gate.sh`) |
| ir_skipped | ~208 |

## Highest-leverage order (after constructible returns)

1. ~~Struct-returning calls / returns (string + `[]int` fields)~~ done for constructible shapes  
2. **Remaining untyped calls** (~36) — often nested/result types still incomplete  
3. **Loop-carried owned values** (~16)  
4. **Array literals** (~12)  
5. **if/else merge after statements** (~12)  
6. **Register allocation**  
7. **Runtime pieces still on C** → Mako syscalls  
8. **Fixed point** stage 1→2→3 + purity audit  

## Safety invariants while growing the backend

- Unsupported → fail closed, never silent fallback to C/Cranelift  
- Owned values: clone-on-pass for strings/slices; struct params borrow  
- Element of `[]Struct` is `IR_ELEM_ADDR` / fused field, never wrong-width `IR_INDEX`  
- Pin ELF hashes; execute on Linux; soaks for mmap/munmap balance  

## Do not

- Port features by growing Rust product surface  
- Update pins without a byte-level reason  
- Claim full self-host before fixed-point + purity audit  
