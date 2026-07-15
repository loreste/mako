# Mako 0.1.6

**mako0.1.6** (`CARGO_PKG_VERSION`) — patch after 0.1.5.

## Highlights

- YAML + TOML encoding packages (`std/encoding/yaml`, `std/encoding/toml`)
- Plugin product (live dylib load/call/reload/manifest)
- Rich collections (set/heap/ring/list stats)
- Full time (calendar, parse/format, duration) + full syscall surface
- Unicode/utf8 depth (UCD seed + encode/decode)

## Install (after GitHub Release assets are published)

```bash
curl -fsSL https://github.com/loreste/mako/releases/download/v0.1.6/install-release.sh \
  | bash -s -- --version v0.1.6 --yes
```

Linux-only:

```bash
curl -fsSL https://github.com/loreste/mako/releases/download/v0.1.6/install-linux.sh \
  | bash -s -- --version v0.1.6
```

Fill brew/winget SHAs after tag CI:

```bash
./scripts/fill-release-packaging.sh v0.1.6
```

See [CHANGELOG.md](../CHANGELOG.md) section **0.1.6**.
