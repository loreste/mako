# Adaptive optimization without in-process recompile

**Goal:** learn from real traffic so the **next** release binary can be better
shaped — without rewriting machine code in the live process, and without a GC.

Tip: **0.4.15+** · Related: [LONG_RUNNING.md](LONG_RUNNING.md) ·
[PERFORMANCE.md](PERFORMANCE.md) · [MEMORY_SAFETY.md](MEMORY_SAFETY.md) ·
[SPEED_SAFE.md](SPEED_SAFE.md) (AOT speed bar coexists with this feedback loop).

---

## Approach

| Property | Mako approach |
|----------|----------------|
| Production traffic | Optional live counters + offline PGO on redeploy |
| When code gets better | Rebuild / redeploy — not mid-request |
| Process start | Full native AOT (`-O3` + LTO on release) |
| Live machine code | Not rewritten in-process |
| Memory while optimizing | No GC; ownership / arenas |

Working rules:

1. Release AOT is the default from process start.  
2. Do not rewrite machine code in a running process.  
3. Live feedback is opt-in and cheap (relaxed atomics / sampling).  
4. Heavy specialization is offline (train → merge → rebuild → ship).

---

## Architecture (three layers)

```text
┌─────────────────────────────────────────────────────────────┐
│  Layer A — AOT always                                        │
│  mako build --release  →  -O3 -flto  (+ optional LLVM)       │
│  No interpreter. No warmup. No GC.                           │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│  Layer B — cheap runtime feedback (optional)                 │
│  hot_site_enable(1) + hot_site_hit(id)   // atomic ++        │
│  profile_sample_*  /  /debug/hot_sites   // export only      │
│  Cost when off: one load + branch. When on: relaxed atomic.  │
└─────────────────────────────────────────────────────────────┘
                              │ export JSON / pprof-text
                              ▼
┌─────────────────────────────────────────────────────────────┐
│  Layer C — offline re-opt (deploy cycle)                     │
│  scripts/pgo-build.sh  ·  scripts/adaptive-opt-cycle.sh      │
│  MAKO_PGO_GEN train on staging → MAKO_PGO_USE production     │
│  Optional: MAKO_ALLOCATOR=mimalloc|jemalloc                  │
└─────────────────────────────────────────────────────────────┘
```

**Never in the hot request path:** clang instrumentation (`-fprofile-generate`),
stack-walking profilers on every call, or mid-flight code patching.

---

## Layer B API (hot sites)

Cooperative site counters — you name the sites that matter:

```mko
fn handle(route: int) -> int {
    // site ids are app-defined (0..255)
    let _ = hot_site_hit(route)
    // … work …
    return 0
}

fn main() {
    let _ = hot_site_enable(1)   // default is off
    // serve traffic …
    let js = hot_sites_json()    // mako.hot_sites.v1
    // or HTTP: profile_http_route("/debug/hot_sites")
}
```

| Function | Role |
|----------|------|
| `hot_site_enable(on)` | Master switch; returns previous mode |
| `hot_site_enabled()` | 0/1 |
| `hot_site_hit(id)` | Record one hit (0 if disabled; −1 if id out of range) |
| `hot_site_count(id)` | Current count for site |
| `hot_site_total()` | Sum of all hits since clear |
| `hot_site_top_id` / `top_count` | Hottest site |
| `hot_site_clear()` | Zero counters |
| `hot_sites_json()` | Compact export (`mako.hot_sites.v1`) |

HTTP seed path (same router as pprof): **`/debug/hot_sites`**.

Stack sampling (heavier; still not an in-process recompile) remains under
`profile_sample_*` — use sparingly; prefer hot sites on the request critical path.

---

## Layer C — production feedback loop

```bash
# One-shot recipe: instrumented train on representative load → optimized binary
./scripts/pgo-build.sh app.mko -o out/app -- <train-args>

# Documented continuous cycle (export guidance + offline PGO):
./scripts/adaptive-opt-cycle.sh app.mko -o out/app
```

Recommended ops loop for years-up services:

1. Ship **release AOT** (LTO; optional mimalloc).
2. Enable `hot_site_*` on a few route/handler ids; scrape `/debug/hot_sites`
   and optional pprof text **out of band**.
3. Nightly/staging: rebuild with `pgo-build.sh` under **real shapes**.
4. Blue/green swap the new binary — **no process ever recompiled itself**.

---

## Tradeoffs (modest)

| Concern | In-process recompile (typical) | Mako adaptive AOT |
|---------|--------------------------------|-------------------|
| Cold start | Often slower until specialized | Full AOT from the start |
| Live rewrite / deopt | Possible | Not used |
| Code growth in-process | Possible | Binary size fixed at deploy |
| GC while specializing | Possible with collectors | No GC |
| p99 early in life | Can be spiky | Tends to be more stable |
| Compiler in the process | Can use significant memory | None |

Online specialization can still outperform naive AOT on some microkernels. Mako
prioritizes **predictable free and RSS** for long runs, and uses offline PGO
when peak throughput on a workload needs another pass.

---

## Claims policy

- Do say: no GC; no live recompile; optional counters; offline PGO.  
- Do not say the binary rewrites itself at runtime.  
- Do not invent throughput numbers without a named soak and hardware (LONG_RUNNING LR-7).

Tests: `examples/testing/hot_site_test.mko`.

**Invariant:** AOT layout opts should leave `hot_site_*` default-off and PGO env
wiring unchanged.
