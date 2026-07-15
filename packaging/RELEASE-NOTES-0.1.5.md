# Mako 0.1.5

**mako0.1.5** (`CARGO_PKG_VERSION`) — patch after 0.1.4.

## Install (after GitHub Release assets are published)

```bash
curl -fsSL https://github.com/loreste/mako/releases/download/v0.1.5/install-release.sh \
  | bash -s -- --version v0.1.5 --yes
```

Linux-only install script:

```bash
curl -fsSL https://github.com/loreste/mako/releases/download/v0.1.5/install-linux.sh \
  | bash -s -- --version v0.1.5
```

Fill brew/winget SHAs after the tag CI finishes:

```bash
./scripts/fill-release-packaging.sh v0.1.5
```

## Highlights since 0.1.4

- Package-per-directory (multi-file packs, path deps + pull)
- Unbuffered rendezvous channels (`chan_new(0)`)
- Go-style implicit interface method sets
- Actor int message payloads (`receive Inc(delta)`)
- Const-fn depth: match, while, for, break/continue, string consts, string `const fn`
- Error chain peel (`error_unwrap` / `root` / `as_tag` / `has_tag`)
- Switch `fallthrough` dual

## Docs

- [CHANGELOG.md](../CHANGELOG.md) section **0.1.5**
- [STATUS.md](../docs/STATUS.md) · [ROADMAP.md](../docs/ROADMAP.md) · [RELEASE.md](../docs/RELEASE.md)

**Full changelog:** https://github.com/loreste/mako/compare/v0.1.4...v0.1.5
