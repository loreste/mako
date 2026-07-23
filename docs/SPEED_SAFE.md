# Speed · memory safe · faster than C and Rust

**Product contract:** Mako is **memory safe without a GC**, and on **published
workloads** the release path is **competitive with or faster than** hand-written
C and Rust — measured, not claimed.

Tip: **0.4.15+** · Related: [SPEED.md](SPEED.md) · [MEMORY_SAFETY.md](MEMORY_SAFETY.md) ·
[PERFORMANCE.md](PERFORMANCE.md) · [LONG_RUNNING.md](LONG_RUNNING.md) ·
[ADAPTIVE_OPT.md](ADAPTIVE_OPT.md) (JIT-like feedback **without** online JIT tax).

---

## Dual bar (both required)

| Axis | Bar | How we enforce it |
|------|-----|-------------------|
| **Memory safe** | No GC; deterministic free; bounds in safe release | Ownership / arenas / share · ASan fixtures · `scripts/memory-safety-gate.sh` |
| **Faster than C/Rust** | Per-workload medians ≤ 1.0× hand-C / Rust where the gate is green | `scripts/native-bench-gate.sh` + baselines JSON |

We do **not** trade safety for speed by turning off checks on the safe path, and
we do **not** accept a GC to “fix” free. Speed comes from:

1. **Native AOT** (`-O3 -flto` / LLVM release) — no interpreter, no JIT warmup tax  
2. **Cheap free** — views (`cap==0`), stack POD lits, immortal strings; free is cold  
3. **Layout + hash** — hand-C-matched `map[int]int`, identity int hash, 50% load pre-size  
4. **Explicit cost** — `share` / channels / arenas only when the program asks  
5. **Adaptive feedback (optional)** — `hot_site_*` + offline PGO; see below  

### Adaptive / “JIT-like” layer stays intact

Map and other micro-opts must **not** replace or slow the adaptive path:

| Layer | Status | Contract |
|-------|--------|----------|
| **A — AOT always** | Required | Full native at t=0; map identity hash is AOT-only |
| **B — `hot_site_*`** | Opt-in | Default **off** (one load + branch); on = relaxed atomic; export `/debug/hot_sites` |
| **C — offline PGO** | Deploy-time | `MAKO_PGO_GEN` / `MAKO_PGO_USE` · `scripts/pgo-build.sh` · `adaptive-opt-cycle.sh` |

**Never** on the live production hot path: `-fprofile-generate`, mid-process
code rewrite, or making `hot_site_hit` mandatory for correctness. Speed work
(layout, hash, LTO) is Layer A; learning from traffic is B→C.

Tests: `examples/testing/hot_site_test.mko` (incl. map + counters).

---

## Current evidence (Apple arm64, 2026-07-23 sample)

After map pre-size (50% load) + identity int hash (same as hand-C / native):

`MAKO_NATIVE_WORKLOADS=native_map ./scripts/native-bench-gate.sh`

| Workload | vs hand-C | vs Rust | Notes |
|----------|-----------|---------|-------|
| **map[int]int** fill+sum 1e6 | **~1.7×** residual | **~0.27×** (**~3.7× faster than Rust**) | Both Mako C and native; owned free |
| **fib** (prior sample) | **~1.01×** | **~1.01×** | Parity |
| **slice** (prior sample) | **~1.13×** | **~1.10×** | Within 1.25× bar |

**Read this honestly:**

- On **map**, Mako is **already much faster than Rust** with **ownership free** (no GC).
- Residual vs **hand-C** is stack header + LICM — not “turn off safety.”
- On **fib**, we are **tied** with hand-C and Rust (within noise).

Update numbers only by re-running the scripts. Do not invent ratios.

---

## How to stay memory safe *and* fast (checklist)

```text
[ ] Prefer stack / non-escaping POD slices in hot loops (cap==0 views)
[ ] make([]T, 0, n) / make(map[K]V, n) — pre-size; avoid rehash/realloc
[ ] No share/RC on the p50 path unless sharing is required
[ ] Request-scoped arena for HTTP / messaging handlers
[ ] Release build: mako build --release (and llvm backend when available)
[ ] Gate before ship: memory-safety-gate + native-bench-gate
[ ] Long-run: long-run-soak / http-long-run-soak (RSS, not just mean latency)
```

---

## Residual C gap (map) — still safe

Hand-C keeps the map **header on the stack** and inlines the probe with
perfect LICM. Mako keeps a **heap header** so ownership drop is reliable.

That is an intentional safety/layout trade: free is deterministic; the
binary still beats Rust on this bench. Closing the rest of the hand-C gap is
LICM / stack-eligible maps for proven non-escaping `make(map)` — **without**
dropping bounds or ownership.

---

## Commands

```bash
# Speed vs C / hand-C / Rust
./scripts/native-bench-gate.sh
# Map only, looser absolute bar while tuning:
MAKO_NATIVE_WORKLOADS=native_map ./scripts/native-bench-gate.sh 2.0

# Memory safe, no GC
./scripts/memory-safety-gate.sh

# Years-up RSS
./scripts/long-run-soak.sh
./scripts/http-long-run-soak.sh
```
