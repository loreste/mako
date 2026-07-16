# Mako 0.1.9

**mako0.1.9** (`CARGO_PKG_VERSION`) — patch after 0.1.8.

## Highlights

**Generics & bounds** (primary):

- Generic **structs** and **enums** with compile-time monomorphization
- Multi-param types (`Triple[A, B]`) and nested generics (`Box[Pair[int]]`)
- Interface bounds: `fn f[T: Describable](x: T)` with structural method checks
- Negative tests reject bound violations

**Seeds** (usable, with known limits):

- Iterator: `next() -> Option[T]` hooks into `for` codegen (by-value `self`
  does not auto-advance — document carefully)
- Mutable closures: heap-cell capture path for assigned captures

## Install (after GitHub Release assets are published)

```bash
curl -fsSL https://github.com/loreste/mako/releases/download/v0.1.9/install-release.sh \
  | bash -s -- --version v0.1.9 --yes
```

Linux-only:

```bash
curl -fsSL https://github.com/loreste/mako/releases/download/v0.1.9/install-linux.sh \
  | bash -s -- --version v0.1.9
```

Fill brew/winget SHAs after tag CI:

```bash
./scripts/fill-release-packaging.sh v0.1.9
```

See [CHANGELOG.md](../CHANGELOG.md) section **0.1.9**.

**Full changelog:** https://github.com/loreste/mako/compare/v0.1.8...v0.1.9
