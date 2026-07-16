# Mako 0.1.8

**mako0.1.8** (`CARGO_PKG_VERSION`) — patch after 0.1.7.

## Highlights

**Speed-first** runtime + codegen wave (no retreat from the 0.1.7 surface):

- **wyhash** map hashing (replaces FNV-1a on string keys)
- **Stack f-string builder** (256 B stack; short interpolations allocate zero)
- **Zero-copy** string literals in `==` / `str_*` / `match` / `print`
- **Compile-time** integer constant folding
- **`select`** wakes via shared condvar (not 2 ms nanosleep polling)
- **HTTP** connection table 32 → **1024** + O(1) atomic active count
- **Lock-free** `chan_cap()`; safer slice append grow (`malloc+copy`)
- Codegen: joined `want_map` keys + `emit_line` (fewer compiler heap allocs)

### Bug fixes

- `http_active_connections()` now tracks live slots correctly
  (`mako_http_conn_set_live`)
- msgpack seed tests aligned with compact encoding

## Install (after GitHub Release assets are published)

```bash
curl -fsSL https://github.com/loreste/mako/releases/download/v0.1.8/install-release.sh \
  | bash -s -- --version v0.1.8 --yes
```

Linux-only:

```bash
curl -fsSL https://github.com/loreste/mako/releases/download/v0.1.8/install-linux.sh \
  | bash -s -- --version v0.1.8
```

Fill brew/winget SHAs after tag CI:

```bash
./scripts/fill-release-packaging.sh v0.1.8
```

See [CHANGELOG.md](../CHANGELOG.md) section **0.1.8**.

**Full changelog:** https://github.com/loreste/mako/compare/v0.1.7...v0.1.8
