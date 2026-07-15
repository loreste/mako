# Packaging

Assets for distributing **Mako** outside a source checkout.

## GitHub Releases (done for v0.1.4)

Built by `.github/workflows/release.yml` on tag `v*`:

| Platform | Artifact |
|----------|----------|
| macOS arm64 | `mako-aarch64-apple-darwin.tar.gz` |
| Linux x86_64 | `mako-x86_64-unknown-linux-gnu.tar.gz` |
| Windows x64 | `mako-x86_64-pc-windows-msvc.zip` |

Installers: `install-release.sh`, `install-linux.sh` (LF only).

```bash
curl -fsSL https://github.com/loreste/mako/releases/download/v0.1.4/install-release.sh \
  | bash -s -- --version v0.1.4 --yes
```

## Fill packaging from a release

After tagging and CI finishes:

```bash
./scripts/fill-release-packaging.sh v0.1.4
```

Updates:

- `packaging/winget/mako.locale.en-US.yaml` (InstallerSha256)
- `Formula/mako.rb` (url + sha256 of source tarball)
- `packaging/RELEASE-CHECKSUMS-*.md`

## Winget (external PR)

Manifest: `packaging/winget/mako.locale.en-US.yaml` (singleton, ManifestVersion 1.6.0).

1. Ensure SHA is filled (`fill-release-packaging.sh`).
2. Fork [microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs).
3. Copy the YAML to `manifests/l/loreste/mako/0.1.4/loreste.mako.yaml` (or multi-file layout).
4. Open a PR. Validation bots will download the installer URL.

Helper:

```bash
./scripts/publish-winget-seed.sh
# optional: wingetcreate submit ... (if wingetcreate is installed)
```

## Homebrew (local tap / homebrew-core)

Formula: `Formula/mako.rb` (stable `url` + `sha256` for `v0.1.4` source).

```bash
brew install --build-from-source Formula/mako.rb
brew audit --strict --online Formula/mako.rb   # before homebrew-core PR
```

**homebrew-core** requires a public formula PR and maintainer review (external).

Prebuilt (no Rust) remains the preferred end-user path via `install-release.sh`.

## MSI / notarize

| Piece | Status |
|-------|--------|
| `packaging/windows/mako.wxs` | WiX skeleton seed |
| `scripts/package-msi-seed.sh` | Dry-run validation |
| `scripts/package-notarize-seed.sh` | Dry-run (needs Apple secrets for real notary) |

Signed MSI / notarized pkg need certificates in CI — not automated without secrets.

## Checksums

See `packaging/RELEASE-CHECKSUMS-0.1.4.md` after running `fill-release-packaging.sh`.
