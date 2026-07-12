# Mako performance

**The name of the game is speed.**  
**Bar: as close to Rust as possible.**

Mako is not a Rust dialect, but on hot paths it must **compete with Rust** —
not with GC languages. Concurrent and parallel work is **first-class** and must
stay fast too (`crew`, `fan`, channels) — see [SPEED.md](SPEED.md).

| Principle | Practice |
|-----------|----------|
| Speed first | Prefer the fast design; convenience features stay off the hot path or opt-in |
| No mandatory GC | Scope cleanup, `hold` / `share` / `arena` — no stop-the-world tax |
| Native codegen | `.mko` → C → clang; release **`-O3 -flto`** |
| Zero-cost default | Everyday constructs should not allocate or synchronize under the hood |
| Explicit cost | Heavier tools (`share`, channels, `crew`) are visible when they cost |
| First-class concurrent/parallel | Language keywords, structured joins — not a slow thread-pool package |
| Measure | Prefer `./scripts/bench-vs-go-rust.sh` and real workloads over vibes |

Targets **backend and systems** workloads: arenas for request scope, tight
slice/map layouts, single static binaries.

Book: [§11 Speed & memory safety](book/src/ch11-speed-safety.md) · Release how-to: [howto/09-release-builds.md](howto/09-release-builds.md).

**Do not invent numbers.** Re-run locally:

```bash
./scripts/bench-vs-go-rust.sh
# optional parsing:
./scripts/bench-vs-go-rust.sh 2>&1 | awk '/=== CPU/,/=== Memory/' | python3 scripts/parse_bench_ns.py

# CI-style gate (fib/slice/map vs Rust; default max 2.0×):
./scripts/bench-gate.sh
./scripts/bench-gate.sh 1.5   # stricter
```

## Publishing benchmark claims

Do not publish a throughput number by itself. Any req/sec claim must include
the test setup and the exact command used to produce it. At
minimum, include:

- Hardware: CPU model, core count, RAM, OS, and whether the client and server
  ran on the same machine.
- Build: Mako commit, `mako build` flags, C compiler, optimization flags, and
  linked optional libraries.
- Server workload: source file, route, response body size, keep-alive setting,
  TLS on/off, logging on/off, and concurrency model.
- Load generator: tool name/version, command line, duration, warmup, concurrent
  connections, threads, pipelining, and whether latency percentiles were
  recorded.
- Result: requests/sec, p50/p95/p99 latency, error count, CPU utilization, and
  peak RSS.

Example format:

```text
HTTP throughput: N req/sec
Server: commit <sha>, `mako build --release examples/http_server.mko -o out/http_server`
Client: `wrk -t8 -c256 -d30s http://127.0.0.1:18080/`
Machine: <CPU>, <RAM>, <OS>; client and server on localhost
Response: <bytes>, keep-alive on, no TLS, access logs off
Latency: p50 <x> ms, p95 <y> ms, p99 <z> ms; errors: 0
```

If any of those details are missing, describe the number as an informal local
experiment, not a benchmark result.

## Measured wall-clock times (median of 5 runs, this machine, 2026-07-09)

Wall ns for each kernel (`now_ns`). Lower is better.
`black_box` prevents LTO from erasing work.

| Kernel | Wall time |
|--------|-----------|
| fib30×5 | 1.42 ms |
| slice100k append | 61 µs |
| map50k pre-sized | 503 µs |

These are narrow local microbenchmarks, not proof of end-to-end service
performance. Use them to sanity-check codegen changes, then measure your own
service workload with the methodology above.

Peak RSS via `/usr/bin/time -l` may be unavailable in restricted sandboxes; run the
script on a normal shell for RSS lines.

## Build profiles

| Profile | Flags | Use |
|---------|-------|-----|
| **Debug** (default) | `-O0 -g` | Dev, tests, ASan (`--sanitize=address`) |
| **Release** (`--release`) | `-O3 -flto -DNDEBUG` | Production — bounds checks elided (see below) |
| Optional strip | `MAKO_STRIP=1` | Smaller deploy artifacts |

`mako profile` builds and runs one program, then reports frontend, backend,
build, run, total wall time, and exit code. `--json` emits the stable
`mako.profile.v1` schema for CI trend collection.

```bash
mako build --release main.mko -o svc
mako profile main.mko --release --json
```

## Memory & CPU practices

1. **`--release`** for anything you measure or ship.
2. **Pre-size** slices/maps: `make([]int, 0, n)`, `make(map[int]int, n)`.
3. **Arenas** for request-scoped strings/buffers — one free, no per-object churn.
4. Prefer **`hold`** over **`share`** when unique ownership works (no RC traffic).
5. Avoid hidden clones: map rehash **moves** keys; HTTP parse copies once into the arena.
6. Measure with **`now_ns`** / `black_box` for microbenches (ms timers hide wins).

## Runtime / codegen wins (shipped)

| Win | Effect |
|-----|--------|
| `now_ns` + `black_box` | Honest ns benches; LTO-safe |
| Map II/SI/SS pre-size to load factor | Fewer rehashes |
| Map rehash move (not clone) | Less alloc/CPU on grow |
| Slice/byte `make`: zero only `len`, not unused `cap` | Less CPU + cleaner pages |
| Fast-path append when `len < cap` | One branch, no realloc check math |
| HTTP: `mako_arena_cstr` / `arena_text_n` | No malloc+arena double copy |
| Empty string singleton | No malloc for `""` |
| Bounds checks under `#ifndef NDEBUG` | Release hot loops use unchecked indexing |

### Release bounds checks (safety)

Debug builds abort on OOB. Release (`-DNDEBUG`) elides slice/byte index checks for
maximum throughput — **OOB is a programmer bug / UB in release**. Prefer debug+ASan
while developing. See [SECURITY.md](SECURITY.md).

## Concurrency

`crew` / channels: pthread sync, **no STW GC**. Prefer bounded channels + request arenas.

## Incremental builds

Hot rebuilds: changed units + link only — [BUILD.md](BUILD.md).
