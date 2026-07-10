# 11. Building for speed & memory safety

## Release builds

```bash
mako build --release main.mko -o bin/app
# → clang -O3 -flto (see PERFORMANCE.md)
```

Debug defaults to `-O0 -g` for fast edit loops. Measure frontend vs clang with
`mako build --time`.

## Incremental & parallel

| Flag / env | Meaning |
|------------|---------|
| (default) | Incremental **on** (`.mako/cache/`) |
| `--no-incremental` | Bypass caches |
| `-j N` / `MAKO_JOBS` | Parallel `clang -c` |
| `MAKO_CACHE` | Cache root override |

Details: [BUILD.md](../../BUILD.md).

## Performance habits

1. Pre-size `make([]T, 0, n)` / `make(map[K]V, n)`.
2. Arena-per-request for temporary buffers.
3. Prefer `hold` over `share` when uniqueness is enough.
4. Avoid needless string copies; use builders (`str_builder` / `bytes.Buffer`).
5. Benchmark/profile: `./scripts/bench-vs-go-rust.sh`, `mako bench`,
   `mako profile --json`.

Mako’s bar: **fast and lean** on the same hardware
when you lean on arenas and skip mandatory GC — [PERFORMANCE.md](../../PERFORMANCE.md).

## Memory safety contract

| Risk | Prevention |
|------|------------|
| Use-after-move | CFG NLL + `hold` |
| Buffer overflow | Bounds checks; `unsafe` opt-out only |
| Orphan threads | Crew cancel_joins |
| Header injection | `http_header_ok` |
| Secret residue | `secret_drop` |
| SQL injection | Parameterized DB APIs |

`[package] systems = true` forbids GC-weakening of hold/share rules if optional
GC appears later. Full write-up: [SECURITY.md](../../SECURITY.md).

## Sanitizers

```bash
mako build --sanitize=address path.mko
mako build --sanitize=thread path.mko
```

Next: [Cross-platform & WASI](ch12-cross-platform.md).
