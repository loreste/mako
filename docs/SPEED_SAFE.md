# Speed and memory safety

Both matter here. Compiled output with no garbage collector, frees that happen
where ownership says they do, and release builds that get measured.

Performance questions only have answers per workload. The numbers in
[PERFORMANCE.md](PERFORMANCE.md) are recorded against small reference programs
in `examples/bench` and are there to catch regressions between releases, not to
rank languages.

Tip is **0.6.5**. Background reading: [SPEED.md](SPEED.md),
[MEMORY_SAFETY.md](MEMORY_SAFETY.md), [PERFORMANCE.md](PERFORMANCE.md),
[LONG_RUNNING.md](LONG_RUNNING.md). Traffic feedback without live recompile —
**Anneal** — is in [ADAPTIVE_OPT.md](ADAPTIVE_OPT.md).

---

## Two bars we won’t trade against each other

Memory first in spirit, even when we’re chasing speed: no GC, free is
deterministic, bounds checks stay on the safe path. The gate for that is
`scripts/memory-safety-gate.sh`.

Speed is the other bar, tracked per named workload by
`scripts/native-bench-gate.sh` against the JSON baselines next to it. A
workload that regresses fails the gate; a workload we are slow on stays in the
table.

Disabling safe-path checks to improve a microbench is out of bounds, and so is
adding a collector. The levers are data layout, ahead-of-time optimization, and
offline PGO where it measurably helps.

---

## What usually moves the needle

Release AOT is the floor: `-O3 -flto`, optional LLVM, no interpreter hanging
around at runtime. Free is cheap in the cases the language already allows —
views with `cap == 0`, stack POD lits, immortal strings. Maps are open
addressing; int keys use identity hash; give a size hint and we pre-size
toward ~50% load so fill doesn’t thrash the table. You pay for share,
channels, and arenas when you ask for them.

If you want feedback from production shapes, `hot_site_*` and offline PGO are
there. They’re optional. Correctness never depends on them.

Roughly three steps, not a self-modifying process:

1. You start from a full native binary.
2. Optionally count hits at a few sites (`hot_site_*`, default off) and export
   via `/debug/hot_sites`.
3. Train offline, rebuild, deploy (`pgo-build.sh`, `adaptive-opt-cycle.sh`).

Don’t hang heavy instrumentation on the request path. Tests for the counters
are in `examples/testing/hot_site_test.mko`.

---

## Numbers from one machine (yours will differ)

Apple arm64, 2026-07-23. Re-run before you quote anything:

```bash
MAKO_NATIVE_WORKLOADS=native_map ./scripts/native-bench-gate.sh
```

On that box, `map[int]int` fill-and-sum over 1e6 entries ran at ratio 1.54
against the C reference program and 0.23 against the Rust one. `fib` measured
near 1.0 against both. Slice sum stayed inside the 1.25 gate with the checked
index still enabled. Ratios are Mako divided by reference, so higher is slower.
The reference programs are small and untuned; see
[PERFORMANCE.md](PERFORMANCE.md) for the full table and the sampling caveats.

Hardware, flags, thermal noise — all of it moves the numbers. The leftover map
gap vs hand-C is partly layout (stack header and freer LICM in the C version),
not “we left safety off.” Don’t invent ratios or stretch these into a general
ranking.

---

## Things that tend to help both speed and steady memory

Pre-size when you know the shape (`make([]T, 0, n)`, `make(map[K]V, n)`). Keep
request work on short-lived owns or arenas. Leave `share` / RC off the common
path unless you need sharing. Ship with `makori build --release` (LLVM if you
have it). Run `memory-safety-gate` and `native-bench-gate` when they apply.
For long-running services, look at RSS with the soak scripts — mean latency
alone won’t catch creep.

---

## Why the map residual exists

Hand-written C often keeps the map header on the stack. Mako keeps a heap
header so ownership drop stays simple. That’s a real cost on microbenches and
a deliberate trade for reliable free. If we get non-escaping maps later, that
has to keep ownership, not throw it away for a prettier number.

---

## Commands

```bash
./scripts/native-bench-gate.sh
MAKO_NATIVE_WORKLOADS=native_map ./scripts/native-bench-gate.sh 2.0

./scripts/memory-safety-gate.sh
./scripts/long-run-soak.sh
./scripts/http-long-run-soak.sh
```
