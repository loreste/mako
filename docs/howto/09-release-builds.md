# Release builds

```bash
mako build --release main.mko -o svc
MAKO_STRIP=1 mako build --release main.mko -o svc
mako build --release -j 8 --time main.mko
```

| Profile | Flags |
|---------|-------|
| Debug (default) | `-O0 -g` — bounds checks on |
| Release | `-O3 -flto -DNDEBUG` — checks elided |

Incremental: unchanged packages reuse `.o` under `.mako/cache/` ([BUILD.md](../BUILD.md)).

Measure performance: `./scripts/bench-vs-go-rust.sh` · [PERFORMANCE.md](../PERFORMANCE.md).

Practices: pre-size maps/slices, arenas per request, prefer `hold` over `share`.
