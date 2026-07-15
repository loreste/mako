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
# GitHub Actions: job "Bench gate vs Rust" on ubuntu-latest
```

**IDs on the hot path:** `Uuid` / ULID are **16-byte Copy POD** (stack, no GC).
Prefer `uuid_v7` / `ulid_new` for time-ordered keys; format to string only at
API boundaries. `uuid_from_bytes` hard-fails on wrong length (memory safety).

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
   Map monomorph C helpers are **demand-driven** (only used `map[K]V` shapes
   are emitted) — still prefer fewer distinct map shapes on hot modules.
3. **Arenas** for request-scoped strings/buffers — one free, no per-object churn.
4. Prefer **`hold`** over **`share`** when unique ownership works (no RC traffic).
5. Avoid hidden clones: map rehash **moves** keys; HTTP parse copies once into the arena.
6. Measure with **`now_ns`** / `black_box` for microbenches (ms timers hide wins).

## Runtime / codegen wins (shipped)

| Win | Effect |
|-----|--------|
| `now_ns` + `black_box` | Honest ns benches; LTO-safe |
| Map II/SI/SS pre-size to ~75% load | Fewer rehashes on sequential insert |
| Map rehash move (not clone) | Less alloc/CPU on grow |
| Map set/get `MAKO_LIKELY` paths | Better branch prediction on hits |
| Slice/byte `make`: zero only `len`, not unused `cap` | Less CPU + cleaner pages |
| Fast-path append when `len < cap` (+ likely) | One branch, no realloc check math |
| HTTP: `mako_arena_cstr` / `arena_text_n` | No malloc+arena double copy |
| Empty string singleton + `mako_str_free` | No malloc for `""`; safe free of singleton |
| `str_clone` / `str_concat` empty fast paths | Less allocator traffic |
| **Release does not force bounds-always** | Index hot path free unless `--bounds always` |
| Bounds checks under `#ifndef NDEBUG` | Debug checks; release elided by default |

### Release bounds checks (safety)

Debug builds abort on OOB. **Default release** (`-DNDEBUG`) elides generated
`MAKO_BOUNDS_CHECK` and most runtime `#ifndef NDEBUG` index checks for
maximum throughput — **OOB is a programmer bug / UB in release**.

To keep checks in production:

```bash
mako build --release --bounds always main.mko -o svc
# or in mako.toml:
# [profile.release]
# bounds_checks = "on"
```

Prefer debug + ASan while developing. See [SECURITY.md](SECURITY.md).

### Audit (2026-07-13): what slowed us / what we fixed

| Issue | Impact | Fix |
|-------|--------|-----|
| `mako build --release` forced `MAKO_BOUNDS_ALWAYS` | Every index paid a branch+compare in release | Default release no longer forces it |
| Empty `mako_str_from_cstr("")` always `malloc` | Alloc pressure on empty strings | Process-wide empty singleton |
| Map grow at 70% load | Extra rehash on dense inserts | Grow at ~75% (`4/3` pre-size) |
| No branch hints on map/append | Mispredict on hot loops | `MAKO_LIKELY` / `UNLIKELY` |
| `map[string]…` always cloned keys | Alloc per insert | `map_si_set_take` / `map_ss_set_take` (move) |
| HTTP parse N× arena copies | Method/path/headers/body each copied | Views into one `conn.raw` buffer |
| `ch.send(s)` always clones string | Alloc per message | `chan_str_send_take` / `chan_str_try_send_take` |
| JSON respond malloc'd Content-Type | Alloc per reply | Interned static `application/json; charset=utf-8` |
| Proxy socket pump small chunks | Extra syscalls | Linux splice 256 KiB + `F_SETPIPE_SZ`; file→socket sendfile |

Still intentional costs (visible when you use them): `share` RC, channel sync,
default `m[k]=v` still clones string keys (safe), default `ch.send(s)` clones
(safe), kick heap-box for multi-word types, opt-in GC.

### Map string keys (hot path)

```mko
// Default: clone key (key still usable)
m["k"] = 1

// Hot path: move ownership of an owned string into the map (no second alloc)
map_si_set_take(m, owned_key, 1)   // map[string]int
map_ss_set_take(m, owned_k, owned_v) // map[string]string
```

Rehash already **moves** owned keys (no clone/free thrash).

### Channel string take-send (hot path)

```mko
let ch = chan_open[string](64)
// Default: clone so caller retains s
let _ = ch.send(s)

// Hot path: move owned temporary (no second alloc)
let _ = chan_str_send_take(ch, owned_msg)
// Non-blocking: 1 queued, 0 full/closed — always consumes the string
let ok = chan_str_try_send_take(ch, owned_msg)
```

### String regions (no substring alloc)

Prefer region builtins over allocating `s[i:j]` when you only need to compare or
search:

```mko
// Good: no temporary string
if str_slice_eq(line, 0, 3, "GET") == 1 { ... }
let comma = str_slice_index(row, 0, len(row), ",")
if str_at_eq(path, 0, "/api/") == 1 { ... }
let b = str_byte_at(s, i)   // 0..255 or -1
```

Same idea as `str_eq` / `str_contains`, scoped to a byte range. Applies to CSV,
paths, config lines, log parsing, wire formats — any general text work.

### HTTP zero-copy + interning

`http_fill_conn` / `http_parse_request` store method, path, body, Host, User-Agent,
Content-Type as **views** into the connection’s durable `raw[]` buffer (one
`memcpy` of the request). Common Content-Type values and header **names** are
interned to static views (`application/json`, `Host`, …) so compares/responses
need no per-request `malloc`. `respond_json` uses the interned JSON type.

Do not free view strings; clone if you need them after the connection reuses the
buffer (`http_next`).

### Proxy zero-copy

`tcp_fd_copy` / `tcp_splice`: Linux uses kernel **splice** (256 KiB chunks, enlarged
pipe via `F_SETPIPE_SZ`) for socket↔socket. Apple/FreeBSD try **sendfile** for
regular file→socket, then fall back to a 64 KiB userspace pump.

## Concurrency

`crew` / channels: pthread sync, **no STW GC**. Prefer bounded channels + request arenas.

## Incremental builds

Hot rebuilds: changed units + link only — [BUILD.md](BUILD.md).
