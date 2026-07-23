# Memory safety · no GC

**Contract (non-negotiable):** Mako has **no garbage collector**. Memory is
freed by **ownership**, **explicit share (RC)**, and **arenas** — deterministically,
on scope exit / move / drop, not by a tracing collector.

**Product tip:** 0.4.11+ · Related: [SOUNDNESS.md](SOUNDNESS.md) ·
[SECURITY.md](SECURITY.md) · [MEMORY_MODEL.md](MEMORY_MODEL.md) ·
[LONG_RUNNING.md](LONG_RUNNING.md) · [SPEED.md](SPEED.md).

---

## What “memory safe, no GC” means in Mako

| Guarantee | How |
|-----------|-----|
| **No tracing GC** | No collector thread, no stop-the-world, no GC mode flag |
| **Deterministic free** | Scope exit, reassign, `break`/`continue`/`return`/`?`, match Own free |
| **Bounds in safe release** | Index checks abort on OOB even under `-O3 -flto` (SAFE-001) |
| **Use-after-move** | CFG NLL + hold/move checker |
| **Double-free guards** | Own drops once; tests under ASan |
| **Optional RC only when asked** | `share` / `ShareInt` / channel clones — **not** a heap-wide GC |

**Not the same as formally verified Rust.** Safe Mako prevents whole classes of
bugs by construction; `unsafe` / FFI sit outside that guarantee. CI runs ASan /
UBSan / TSan as evidence, not as the only line of defense.

---

## Memory tools (pick one cost model)

| Tool | Role | Free when |
|------|------|-----------|
| **Own** (default for heap strings/slices/maps) | Single owner | Scope exit / move / drop |
| **`hold`** | Move discipline | Same as Own after move rules |
| **`share` / Sync handles** | Explicit multi-owner / concurrent | RC hits zero or lock drop |
| **`arena`** | Bulk region | Arena scope exit (no per-object free tax) |
| **Views** (`cap==0`, `string_view`) | Zero-copy borrow | Never free backing storage |

There is **no** “let the GC clean it up later.”

---

## What is *not* GC

| Mechanism | Why it is not GC |
|-----------|------------------|
| Scope drops | Compiler-inserted free at known CFG points |
| Arena bulk free | Region free once; O(1) teardown |
| `ShareInt` / RC clones | Fixed refcount on a handle — not heap tracing |
| Channel clone/take | Message ownership transfer, not collection |

---

## Verification (how we know)

| Layer | Evidence |
|-------|----------|
| **Typecheck / NLL** | Use-after-move, kick Send, capture rules |
| **Codegen drops** | Free on all exits (SAFE-006 matrix) |
| **Unit tests** | `own_*`, `double_free_guard`, `leak_detector`, `match_own_free`, … |
| **ASan CI** | Full suite + ownership fixtures (`--sanitize address`) |
| **UBSan / TSan** | Undefined behavior + races (opt-in / CI jobs) |
| **Years-up soaks** | `long-run-soak`, `http-long-run-soak` — no RSS creep under alloc/free |
| **Gate script** | `./scripts/memory-safety-gate.sh` |

```bash
# Ownership + leak tests on C and native; ASan when the toolchain supports it:
./scripts/memory-safety-gate.sh
```

---

## Unsafe boundary

| Safe Mako | Outside the guarantee |
|-----------|------------------------|
| Indexed access (bounds-checked) | `unsafe { … }`, `unsafe_index` |
| Own / hold / arena | Raw FFI pointers you free yourself |
| Kick Send checks | Data races if you break Sync rules in C |

---

## Long-running servers

No GC is why Mako targets **years-up** p99 stability. Combine:

1. Request-scoped owns / arenas  
2. `./scripts/long-run-soak.sh` and `./scripts/http-long-run-soak.sh`  
3. Optional `MAKO_ALLOCATOR=mimalloc|jemalloc` for fragmentation  

See [LONG_RUNNING.md](LONG_RUNNING.md).

---

## Claims policy

- **Do** say: no tracing GC; ownership free; bounds in release; ASan suite green.  
- **Do not** say: “memory-safe for all FFI” or “proven like seL4.”  
- **Do not** add a collector later under the same product name without a major version and identity break.
