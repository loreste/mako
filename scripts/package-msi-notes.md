# Windows MSI / winget packaging seed

Mako does not yet ship a signed MSI from CI. This document is the **seed**
workflow for maintainers.

## Portable zip (current)

```powershell
cargo build --release
./scripts/package-release.ps1
# → dist/mako-*-pc-windows-*.zip + .sha256
```

## MSI (optional tooling)

1. Install [WiX Toolset](https://wixtoolset.org/) v3 or v4.
2. Author `packaging/windows/mako.wxs` (not yet committed as product).
3. Harvest `bin/mako.exe` + `share/mako/runtime` + `std`.
4. `candle` / `light` → `mako-0.1.4-x64.msi`.

## winget

Manifest seed: `packaging/winget/mako.locale.en-US.yaml`.

1. Publish GitHub release assets with SHA-256.
2. Replace `InstallerSha256` and `PackageVersion`.
3. Open a PR against [microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs).

Notarization is **not** applicable to Windows; for macOS see `package-macos-notarize-notes.md`.
