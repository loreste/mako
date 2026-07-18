# Mako 0.2.4

**mako0.2.4** (`CARGO_PKG_VERSION`) — soundness wave and efficiency release after 0.2.3.

## Highlights

- Memory-safe drops by construction (slices, maps, strings, structs, `?`, break/continue)
- `string_view` + stack POD array lits (no malloc tax on hot loops)
- Opt-in scheduler pool (`sched_set_workers`) and channel take-send ownership
- Build-time locked dependency verification (PR #3)

## Install (after GitHub Actions publish assets)

```bash
curl -fsSL https://github.com/loreste/mako/releases/download/v0.2.4/install-release.sh \
  | bash -s -- --version v0.2.4 --yes
```

Linux:

```bash
curl -fsSL https://github.com/loreste/mako/releases/download/v0.2.4/install-linux.sh \
  | bash -s -- --version v0.2.4
```

## Packaging maintainers

```bash
./scripts/fill-release-packaging.sh v0.2.4
```

See [CHANGELOG.md](../CHANGELOG.md) section **0.2.4**.

**Full changelog:** https://github.com/loreste/mako/compare/v0.2.3...v0.2.4
