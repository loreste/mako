# Anneal — adaptive optimization

**Anneal** is Makori's adaptive-optimization loop: ship a fully compiled binary,
take a cheap reading of what runs hot in real traffic, and fold that reading
back into the *next* build — repeated over a service's long life. The name is
metallurgical: annealing settles a material into a stronger, more stable
structure through repeated, *offline* heat-and-cool cycles, never by reworking
it while it's in use. Same here — hot-site counters are the heat map, each
offline train/rebuild/swap is one cycle, and the process never rewrites itself.

Most languages that "learn" from a running program do it live: they watch the
process, then rewrite its machine code in place while requests are in flight.
That buys peak throughput on a few hot kernels, but it costs you a warmup
period, a code-generating optimizer sitting inside your process, occasional
de-optimization stalls, and — usually — a garbage collector to go with it.

Mako takes the other side of that trade. Your program is fully compiled ahead of
time and stays that way: no interpreter, no warmup tier, no optimizer living in
the process, and no tracing collector. The binary you deploy is the binary that
runs, start to finish.

You still get to learn from real traffic — just offline. Ship the compiled
binary, let it tell you (cheaply) which paths actually run hot in production,
then fold that knowledge into the *next* build and swap it in. The process never
patches itself; the next artifact is simply better shaped for the work it's
doing. Available in **0.4.15+**.

See also [LONG_RUNNING.md](LONG_RUNNING.md), [PERFORMANCE.md](PERFORMANCE.md),
and [SPEED_SAFE.md](SPEED_SAFE.md).

## The loop

1. **Ship a release build.** `makori build --release` gives you `-O3 -flto` (and
   the optimizing LLVM backend where enabled). This is the default and needs no
   extra setup.
2. **Observe, cheaply.** If traffic shape matters, turn on a handful of hot-site
   counters at the routes you care about and scrape them out of band. They're
   off by default and cost roughly a branch and a relaxed atomic when on.
3. **Retrain offline.** Feed a profiling run into a rebuild
   (`MAKO_PGO_GEN` to record, `MAKO_PGO_USE` to apply). This is where the heavy
   specialization happens — on your build machine, not in production.
4. **Swap.** Deploy the new binary the usual way (blue/green, rolling — your
   call). Nothing in the fleet rewrote itself.

## What lives in the binary vs. offline

**Always on — the compiled binary.** Full ahead-of-time compilation, ownership
frees, no warmup, no collector. Nothing below changes that.

**Opt-in — hot sites.** Name the spots you want to watch and increment a counter
when they run. Disabled by default; when enabled, a hit is a relaxed atomic add.
Read the tallies with `hot_sites_json()` or over HTTP at `/debug/hot_sites`
(next to the pprof routes). Stack sampling (`profile_sample_*`) is heavier —
keep it off the request path unless you mean it.

**Offline — the rebuild.** `scripts/pgo-build.sh` and
`scripts/adaptive-opt-cycle.sh` drive a train/merge/rebuild cycle. Point
`MAKO_PGO_GEN` at a staging run, rebuild production with `MAKO_PGO_USE`. If long
soaks show fragmentation, `MAKO_ALLOCATOR=mimalloc|jemalloc` is there to try.
Keep instrumentation that walks the stack on every call, or that generates code
against a live process, out of your production hot paths.

## Hot-site API

Sites are yours to name — application-defined ids in the range `0..255`
(`MAKO_HOT_SITE_MAX` = 256). Everything is off until you flip the master switch.

```mko
fn handle(route: int) -> int {
    let _ = hot_site_hit(route)   // no-op while disabled
    // … work …
    return 0
}

fn main() {
    let _ = hot_site_enable(1)    // default is off
    // … serve traffic …
    let js = hot_sites_json()     // "mako.hot_sites.v1"
    // or expose it: profile_http_route("/debug/hot_sites")
}
```

| Function | What it does |
|----------|--------------|
| `hot_site_enable(on)` | Master switch; returns the previous mode |
| `hot_site_enabled()` | `0` / `1` |
| `hot_site_hit(id)` | Record one hit (`0` if disabled, `-1` if `id` is out of range) |
| `hot_site_count(id)` | Hit count for a site |
| `hot_site_total()` | Sum of hits since the last clear |
| `hot_site_top_id()` / `hot_site_top_count()` | The hottest site and its count |
| `hot_site_clear()` | Zero the counters |
| `hot_sites_json()` | Compact export, format `mako.hot_sites.v1` |

The HTTP export sits alongside pprof at `/debug/hot_sites`.

## A cycle for long-running services

```bash
./scripts/pgo-build.sh app.mko -o out/app -- <train-args>
./scripts/adaptive-opt-cycle.sh app.mko -o out/app
```

Ship the release build first. Enable `hot_site_*` on a few route-handler ids and
scrape `/debug/hot_sites` out of band. Nightly, run a profiling build on staging
under traffic that looks like production, rebuild with the profile, and swap
blue/green. No node in the fleet recompiles itself.

## Trade-offs, honestly

Live in-process specialization can win on tight kernels once it's warm. It also
tends to bring cold-start cost, de-opt stories, code growth inside the process,
and often a collector paired with the embedded optimizer. Makori's default trades
that peak for predictability: a fixed binary you deploy, ownership-based frees,
steady RSS over months, and no compiler in the process. That's the right default
for services that need to stay boring for a long time. When a specific kernel
genuinely wants online specialization, the offline PGO cycle is the tool to
reach for first.

## What we do and don't claim

**True:** no collector, no live recompile, optional counters that are off by
default, and an offline PGO cycle you run yourself.

**Not claimed:** that the binary rewrites itself, or any throughput number
without a named soak and hardware behind it (see LONG_RUNNING LR-7).

Tests: `examples/testing/hot_site_test.mko` (C and native). Layout work on the
compiled path should leave the `hot_site_*` defaults (off) and the PGO
environment wiring alone.
