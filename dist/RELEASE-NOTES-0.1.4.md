## Mako 0.1.4

**mako0.1.4** — patch after 0.1.3.

### Install (no Rust required)

**macOS (Apple Silicon)**
```bash
curl -fsSL https://github.com/loreste/mako/releases/download/v0.1.4/install-release.sh | bash -s -- --version v0.1.4
# or:
# curl -fsSL …/install-release.sh | bash   # latest
source "$HOME/.local/share/mako/env.sh"  # if prompted
mako version
mako doctor
```

**Linux (x86_64)**
```bash
curl -fsSL https://github.com/loreste/mako/releases/download/v0.1.4/install-linux.sh | bash -s -- --version v0.1.4
```

**Windows**
Download `mako-x86_64-pc-windows-msvc.zip` from this release, extract, run `scripts\install.ps1` from the bundle (or place `mako.exe` on PATH and set `MAKO_RUNTIME`).

### Artifacts

| Platform | Artifact |
|----------|----------|
| macOS arm64 | `mako-aarch64-apple-darwin.tar.gz` + `.sha256` |
| Linux x86_64 | `mako-x86_64-unknown-linux-gnu.tar.gz` + `.sha256` |
| Windows x64 | `mako-x86_64-pc-windows-msvc.zip` + `.sha256` |
| Installers | `install-release.sh`, `install-linux.sh` |

Verify: `shasum -a 256 -c mako-*.sha256` (or `sha256sum -c`).

### Highlights

- Language: zero-copy string regions (`str_slice_*`, `str_byte_at`); const `if`/comparisons fold
- Storage: bloom filters, range scans, disk page manager seeds
- Observability: OTLP protobuf seed, sampling profiler, `mako dap` / `mako profile-serve`
- Packaging: MSI/notarize/Homebrew/winget seeds; multi-OS release CI
- Netcode/hot-reload: prediction seed, live dylib plugin poll seed
- SIP/SQL polish retained from 0.1.3 line (RFC NAT/SDP, SQL bind arity)

### Full notes

See [CHANGELOG.md](https://github.com/loreste/mako/blob/v0.1.4/CHANGELOG.md) section **0.1.4**.

**Full Changelog**: https://github.com/loreste/mako/compare/v0.1.3...v0.1.4
