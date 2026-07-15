# Mako 0.1.7

**mako0.1.7** (`CARGO_PKG_VERSION`) — patch after 0.1.6.

## Highlights

- **CBOR + MessagePack** binary encode/decode (int/bool/nil-null/string/`[]int`)
- **List combinators** (take/drop/zip/map_add/filter/fold …)
- **Avro** binary (long/bool/null/string/array[long])
- **GraphQL** package extras (`std/graphql`)
- **Protobuf** package (`std/encoding/protobuf`)
- **Named TZ offsets** (`time_offset_named` / `time_format_offset`)

0.1.6 already shipped YAML/TOML, plugin product, rich collections, full time/syscall, unicode.

## Install (after GitHub Release assets are published)

```bash
curl -fsSL https://github.com/loreste/mako/releases/download/v0.1.7/install-release.sh \
  | bash -s -- --version v0.1.7 --yes
```

Linux-only:

```bash
curl -fsSL https://github.com/loreste/mako/releases/download/v0.1.7/install-linux.sh \
  | bash -s -- --version v0.1.7
```

Fill brew/winget SHAs after tag CI:

```bash
./scripts/fill-release-packaging.sh v0.1.7
```

See [CHANGELOG.md](../CHANGELOG.md) section **0.1.7**.
