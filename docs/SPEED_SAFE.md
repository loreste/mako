# Speed and memory safety

Mako aims for **native speed without a GC**: ownership free, safe-by-default
checks, and measured release builds. Claims are **per workload**, not universal.

Tip: **0.4.15+** · Related: [SPEED.md](SPEED.md) · [MEMORY_SAFETY.md](MEMORY_SAFETY.md) ·
[PERFORMANCE.md](PERFORMANCE.md) · [LONG_RUNNING.md](LONG_RUNNING.md) ·
[ADAPTIVE_OPT.md](ADAPTIVE_OPT.md) (traffic feedback without live recompile).

---

## What we optimize for

| Axis | Intent | How we check |
|------|--------|----------------|
| **Memory** | No GC; deterministic free; bounds on the safe path | Ownership / arenas / share · `scripts/memory-safety-gate.sh` |
| **Speed** | Stay competitive with hand-C and Rust on **named** benches | `scripts/native-bench-gate.sh` + baselines JSON |

We do not turn off safe-path checks for speed, and we do not add a collector to
simplify free. Layout, AOT opts, and optional PGO are the usual levers.

### Design choices that affect speed

1. **Native AOT** (`-O3 -flto` / optional LLVM) — no interpreter at runtime  
2. **Cheap free where possible** — views (`cap==0`), stack POD lits, immortal strings  
3. **Map layout** — open addressing; int keys use identity hash; pre-size toward ~50% load when a hint is given  
4. **Explicit cost** — `share` / channels / arenas when the program asks  
5. **Optional feedback** — `hot_site_*` (default off) and offline PGO for redeploys  

### Adaptive feedback (optional)

| Layer | Role |
|-------|------|
| **A — AOT** | Full native at process start |
| **B — `hot_site_*`** | Opt-in counters; default off; export `/debug/hot_sites` |
| **C — offline PGO** | Staging train → rebuild → deploy (`pgo-build.sh`, `adaptive-opt-cycle.sh`) |

Production hot paths should not rely on heavy instrumentation. Counters are
optional; correctness does not depend on them.

Tests: `examples/testing/hot_site_test.mko`.

---

## Sample measurements (not a blanket claim)

Host: Apple arm64, 2026-07-23. Re-run locally before quoting.

`MAKO_NATIVE_WORKLOADS=native_map ./scripts/native-bench-gate.sh`

| Workload (sample) | Rough vs hand-C | Rough vs Rust | Notes |
|-------------------|-----------------|---------------|--------|
| map[int]int fill+sum 1e6 | ~1.7× slower | ~0.27× (faster on this host) | Owned map; free on scope exit |
| fib (earlier sample) | ~parity | ~parity | No heap |
| slice sum (earlier sample) | within ~1.25× gate | within gate | Checked index on safe path |

**Caveats:** numbers move with hardware, flags, and thermal noise. Residual map
gap vs hand-C is partly layout (stack header / LICM), not “turn off safety.”
Do not invent or extrapolate ratios.

---

## Habits that help both speed and steady memory

```text
[ ] Pre-size: make([]T, 0, n), make(map[K]V, n)
[ ] Prefer short-lived owns / arenas for request work
[ ] Avoid share/RC on the common path unless sharing is required
[ ] Release build: mako build --release (LLVM when available)
[ ] Before ship: memory-safety-gate + native-bench-gate when relevant
[ ] Long-run: long-run-soak / http-long-run-soak for RSS, not only mean latency
```

---

## Map residual (honest)

Hand-written C often keeps the map header on the stack. Mako keeps a heap
header so ownership drop stays simple and reliable. That can cost a bit on
microbenches while still freeing correctly. Further gains (e.g. non-escaping
maps) should preserve ownership, not drop it.

---

## Commands

```bash
./scripts/native-bench-gate.sh
MAKO_NATIVE_WORKLOADS=native_map ./scripts/native-bench-gate.sh 2.0

./scripts/memory-safety-gate.sh
./scripts/long-run-soak.sh
./scripts/http-long-run-soak.sh
```
