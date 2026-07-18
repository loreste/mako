# Mako 0.2.3

**mako0.2.3** (`CARGO_PKG_VERSION`) — security patch after 0.2.2.

## Highlights

**JWT / JWKS hardening**

- Strict JSON number/literal/object/array skip with depth limit
- Reject trailing junk and malformed JWKS (`jwt_verify_jwks` fails closed)
- Safer `jwt_sign` / `jwt_verify` (payload cap, HMAC length, free buffers)

**HTTPS / listen**

- Dual-stack HTTP listen (shared TCP backlog helper)
- Hardened TLS live tests (ports, SNI bind asserts)

**Docs**

- Verified HTTPS vs cleartext `http_*`, OIDC, JWT SNI contracts

## Install (after GitHub Release assets are published)

```bash
curl -fsSL https://github.com/loreste/mako/releases/download/v0.2.3/install-release.sh \
  | bash -s -- --version v0.2.3 --yes
```

Linux-only:

```bash
curl -fsSL https://github.com/loreste/mako/releases/download/v0.2.3/install-linux.sh \
  | bash -s -- --version v0.2.3
```

Fill brew/winget SHAs after tag CI:

```bash
./scripts/fill-release-packaging.sh v0.2.3
```

See [CHANGELOG.md](../CHANGELOG.md) section **0.2.3**.

**Full changelog:** https://github.com/loreste/mako/compare/v0.2.2...v0.2.3
