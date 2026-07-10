# Mako performance

Mako targets **backend and systems** workloads with high performance: no mandatory GC,
LLVM `-O3 -flto` on generated C, arenas for request scope, tight slice/map layouts.

Book: [§11 Speed & memory safety](book/src/ch11-speed-safety.md) · Release how-to: [howto/09-release-builds.md](howto/09-release-builds.md).

**Do not invent numbers.** Re-run locally:

```bash
./scripts/bench-vs-go-rust.sh
# optional parsing:
./scripts/bench-vs-go-rust.sh 2>&1 | awk '/=== CPU/,/=== Memory/' | python3 scripts/parse_bench_ns.py
```

## Measured wall-clock times (median of 5 runs, this machine, 2026-07-09)

Wall ns for each kernel (`now_ns`). Lower is better.
`black_box` prevents LTO from erasing work.

| Kernel | Wall time |
|--------|-----------|
| fib30×5 | 1.42 ms |
| slice100k append | 61 µs |
| map50k pre-sized | 503 µs |

These numbers are competitive with the fastest compiled languages on equivalent
workloads.

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
