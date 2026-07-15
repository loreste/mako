# Packaging

Assets for distributing **Mako** outside a source checkout.

## GitHub Releases

**Product tip:** **v0.1.7** (after tag + CI). **v0.1.6** was the previous product tip.

Built by `.github/workflows/release.yml` on tag `v*`:

| Platform | Artifact |
|----------|----------|
| macOS arm64 | `mako-aarch64-apple-darwin.tar.gz` |
| Linux x86_64 | `mako-x86_64-unknown-linux-gnu.tar.gz` |
| Windows x64 | `mako-x86_64-pc-windows-msvc.zip` |

Installers: `install-release.sh`, `install-linux.sh` (LF only).

```bash
curl -fsSL https://github.com/loreste/mako/releases/download/v0.1.7/install-release.sh \
  | bash -s -- --version v0.1.7 --yes
```

## Fill packaging from a release

After tagging and CI finishes:

```bash
./scripts/fill-release-packaging.sh v0.1.7
```

Updates:

- `packaging/winget/mako.locale.en-US.yaml` (InstallerSha256)
- `Formula/mako.rb` (url + sha256 of source tarball)
- `packaging/RELEASE-CHECKSUMS-*.md`

## Winget (external PR)

Manifest: `packaging/winget/mako.locale.en-US.yaml` (singleton, ManifestVersion 1.6.0).

**v0.1.4 PR:** https://github.com/microsoft/winget-pkgs/pull/402823  
**v0.1.7:** re-run fill after tag; path `manifests/l/loreste/mako/0.1.7/`.

Portable nested path (must match zip layout from `package-release.ps1`):

```text
mako-x86_64-pc-windows-msvc\bin\mako.exe
```

1. Ensure SHA is filled (`fill-release-packaging.sh`).
2. Fork [microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs).
3. Copy the YAML to `manifests/l/loreste/mako/0.1.7/loreste.mako.yaml`.
4. Open a PR. If bots ask for CLA, comment:  
   `@microsoft-github-policy-service agree`

```bash
./scripts/publish-winget-seed.sh
```

## Homebrew (local tap / homebrew-core)

Formula: `Formula/mako.rb` (stable `url` + `sha256` for source tag — fill after release).

Local tap (already usable on this machine):

```bash
brew tap loreste/mako-local   # if not already
brew install --build-from-source loreste/mako-local/mako
/opt/homebrew/opt/mako/bin/mako version
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

See `packaging/RELEASE-CHECKSUMS-0.1.7.md` after running `fill-release-packaging.sh`
(or `RELEASE-CHECKSUMS-0.1.4.md` for the previous release).
