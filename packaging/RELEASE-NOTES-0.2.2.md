# Mako 0.2.2

**mako0.2.2** (`CARGO_PKG_VERSION`) — patch after 0.2.1.

## Highlights

**TLS SNI (multi-cert servers)**

- Concurrent-safe SNI set rebuild/swap on add
- `tls_server_sni_update` / `tls_server_sni_remove`
- Client hostname verification when peer verify is on

**HTTPS client + identity**

- `https_get` / `https_post` / `https_request` (+ last status/headers)
- `oidc_discovery` / `oidc_token`
- `jwt_verify_rs256` / `jwt_verify_jwks`

**Packaging**

- SHA-256 lockfile integrity v2 (verify on install)

## Install (after GitHub Release assets are published)

```bash
curl -fsSL https://github.com/loreste/mako/releases/download/v0.2.2/install-release.sh \
  | bash -s -- --version v0.2.2 --yes
```

Linux-only:

```bash
curl -fsSL https://github.com/loreste/mako/releases/download/v0.2.2/install-linux.sh \
  | bash -s -- --version v0.2.2
```

Fill brew/winget SHAs after tag CI:

```bash
./scripts/fill-release-packaging.sh v0.2.2
```

See [CHANGELOG.md](../CHANGELOG.md) section **0.2.2**.

**Full changelog:** https://github.com/loreste/mako/compare/v0.2.1...v0.2.2
