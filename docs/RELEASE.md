# Mako release & cross-platform guide

**Product version:** **0.1.8** (`Cargo.toml` / `mako version` → `mako0.1.8`).  
STATUS north-star / MVP: **100%**. External: homebrew-core publish (see STATUS).

## Prerequisites by OS

| OS | Rust | C compiler | Notes |
|----|------|------------|-------|
| **macOS** | rustup | Xcode clang | `xcode-select --install` |
| **Linux** | rustup | `clang` (apt/dnf) | `-pthread -ldl` linked automatically |
| **Windows** | rustup | **LLVM clang** on PATH | Install [LLVM](https://llvm.org/) or `choco install llvm`; MSVC Build Tools recommended for host libs. Runtime uses Winsock2 (`-lws2_32`), not pthreads. |

Optional (native only): OpenSSL, libnghttp2, SQLite, libpq, quiche FFI.

## Build & install (native)

### One-shot prebuilt install (out of the box — no Rust)

**Linux**

```bash
curl -fsSL https://github.com/loreste/mako/releases/latest/download/install-linux.sh | bash
source "$HOME/.local/share/mako/env.sh"
mako version
```

**macOS**

```bash
curl -fsSL https://github.com/loreste/mako/releases/latest/download/install-release.sh | bash
source "$HOME/.local/share/mako/env.sh"
```

What the installer does:

| Step | Detail |
|------|--------|
| System deps | Installs **clang** if missing (apt/dnf/pacman/apk/zypper; macOS hints Xcode CLT) |
| Download | **One** slim platform tarball + `.sha256` (no Rust, no git clone) |
| Verify | SHA-256 (`sha256sum` / `shasum` / `openssl`) |
| Install | `PREFIX/bin/mako` + `share/mako/{runtime,std}` (default `~/.local`) |
| Env | Writes `share/mako/env.sh`; appends source line to `~/.bashrc` / `~/.zshrc` when piped |
| Check | Runs `mako doctor` |

| Bootstrap needs | Why |
|-----------------|-----|
| `curl`, `tar` | fetch + extract |
| `sudo` (Linux, if no clang) | install clang package |

Flags: `--no-deps`, `--no-shell`, `--yes`, `--prefix`, `--version`,
`--base-url` / `MAKO_RELEASE_BASE_URL` (`file://…` for local smoke).

### Package a slim release (maintainers)

```bash
cargo build --release
./scripts/package-release.sh                  # slim (default): strip + no full docs/editors
./scripts/package-release.sh --full           # include docs + VS Code scaffold
# → dist/mako-<triple>.tar.gz + .sha256 + install-linux.sh + install-release.sh
```

### From source (large — Rust toolchain + crates)

```bash
cargo build --release
./scripts/install.sh                 # → $HOME/.local/bin/mako + share/mako/runtime
# or
make install
mako --version
mako doctor
mako test examples/testing
```

### Windows (PowerShell)

```powershell
cargo build --release
.\scripts\install.ps1
# Ensure clang.exe is on PATH, then:
mako --version
mako doctor
mako test examples/testing
```

Runtime headers land in `$PREFIX/share/mako/runtime` (Unix) or `%USERPROFILE%\.local\share\mako\runtime` (Windows default). The installer also copies `std/` and the VS Code scaffold under `$PREFIX/share/mako/`. Discovery: `MAKO_RUNTIME` → binary-relative `../share/mako/runtime` → checkout `./runtime`.

Uninstall:

```bash
./scripts/uninstall.sh --dry-run
./scripts/uninstall.sh
```

```powershell
.\scripts\uninstall.ps1 -DryRun
.\scripts\uninstall.ps1
```

Update an installed prefix from a source checkout:

```bash
mako update --from . --prefix "$HOME/.local"
mako doctor
```

```powershell
mako update --from . --prefix "$env:USERPROFILE\.local"
mako doctor
```

## Cross-compile (`.mko` → foreign binary)

```bash
mako build main.mko --target <triple> -o out
```

| Triple | Typical host | Toolchain |
|--------|--------------|-----------|
| `x86_64-unknown-linux-gnu` | macOS/Windows | **zig cc** (auto if `zig` on PATH) or clang + Linux sysroot |
| `x86_64-unknown-linux-musl` | Linux/macOS/Windows | **zig cc**; static by default |
| `aarch64-unknown-linux-gnu` | Linux x86_64 | zig / cargo-zigbuild for the **mako CLI**; `mako build --target …` for programs |
| `aarch64-unknown-linux-musl` | Linux x86_64 | **zig cc**; static by default |
| `x86_64-pc-windows-gnu` | Linux/macOS | **zig cc** (preferred) |
| `x86_64-pc-windows-msvc` | Windows native | clang-cl / MSVC; cross from Unix usually use `-gnu` via zig |
| `aarch64-apple-darwin` / `x86_64-apple-darwin` | Linux | Needs **macOS SDK** + clang; zig can target `*-macos` but linking Apple frameworks still needs SDK |
| `wasm32-wasip1` | any | wasi-sdk (`WASI_SDK_PATH`) or zig |

**Env knobs**

| Var | Effect |
|-----|--------|
| `MAKO_CC` | Force C driver (`clang`, `/usr/bin/clang`, `zig`, …) |
| `MAKO_USE_ZIG=1` | Prefer `zig cc` when zig is installed |
| `WASI_SDK_PATH` | wasi-sdk root for wasm |

Cross builds skip host OpenSSL/nghttp2/sqlite/libpq/quiche auto-link (sysroots rarely match). Core runtime (threads, files, HTTP plain sockets) is portable.

Static linking policy:

- Linux musl targets such as `x86_64-unknown-linux-musl` default to `-static`.
- Use `--static-link` to request static linking on other non-macOS/non-WASM targets.
- Use `--no-static-link` to opt out of the musl static default.
- macOS and WASM do not use `-static`; glibc Linux remains dynamic by default because fully static glibc builds are often brittle.

Example (Linux → Windows):

```bash
# zig recommended: https://ziglang.org/
mako build hello.mko --target x86_64-pc-windows-gnu -o hello.exe
mako build hello.mko --target x86_64-unknown-linux-musl -o hello-static
```

Minimal containers:

```bash
mako deploy docker . --entry main.mko --bin server --port 8080
docker build -t mako-server .
mako deploy serverless . --provider cloud-run --name mako-server --image gcr.io/PROJECT_ID/mako-server:latest
mako deploy serverless . --provider fly --name mako-server
```

The default Dockerfile builds a static musl binary and copies it into `scratch`.
Use `--mode debian` for a small Debian runtime image with CA certificates.
Serverless helpers generate the same Dockerfile plus provider manifests for
container-native platforms; they do not yet implement native Lambda/Workers
request adapters.

Browser/edge WASM starter:

```bash
mako deploy wasm wasm-dist --entry examples/wasi_hello.mko --wasm hello.wasm --port 8080
./wasm-dist/build-wasm.sh
python3 -m http.server -d wasm-dist 8080
```

This uses the current WASI preview1 output and JS loader/polyfill. Preview2
components, WIT generation, DOM bindings, and native Workers/Lambda request
adapters remain target work.

Plugin ABI seed:

```bash
mako deploy plugin my-plugin --name my-plugin --kind native
mako deploy plugin my-wasm-plugin --name my-wasm-plugin --kind wasm
```

Release archives include `runtime/mako_plugin.h` and `docs/ABI.md`. This is the
stable ABI/skeleton seed; host-side dynamic loading and WASM component adapters
remain target work.

## CI

`.github/workflows/ci.yml`:

| Job | Runner | What |
|-----|--------|------|
| `native` | `ubuntu-latest`, `macos-latest`, `windows-latest` | `cargo build --release`, `mako test examples/testing`, init smoke |
| `cross-smoke` | `ubuntu-latest` | `mako build --target x86_64-pc-windows-gnu` and `x86_64-unknown-linux-musl` via zig → artifacts; musl smoke verifies static link |

Live TLS/QUIC/nghttp2 tests stay opt-in (`MAKO_LIVE_*`); default suite skips without deps on all OSes.

## Release artifacts

`.github/workflows/release.yml` (tag `v*` or dispatch) + local scripts:

| Artifact | Script |
|----------|--------|
| `mako-aarch64-apple-darwin` (+ `.tar.gz`) | `scripts/package-release.sh` on Apple Silicon runner |
| `mako-x86_64-apple-darwin` | same on Intel Mac (or document Rosetta) |
| `mako-x86_64-unknown-linux-gnu` | `package-release.sh` on ubuntu |
| `mako-aarch64-unknown-linux-gnu` | optional zig/`cargo zigbuild` job |
| `mako-x86_64-pc-windows-msvc.exe` (+ `.zip`) | `scripts/package-release.ps1` |

Layout inside tarball/zip:

- `bin/mako` / `bin/mako.exe`
- `share/mako/runtime/*.h`
- `share/mako/std/`
- `share/mako/editors/vscode/`
- `share/mako/docs/`
- `scripts/install.*`
- `scripts/install-release.sh`
- `scripts/uninstall.*`
- sibling `.sha256` checksum file in `dist/`

```bash
./scripts/package-release.sh mako-aarch64-apple-darwin
# Windows:
.\scripts\package-release.ps1 -ArtifactName mako-x86_64-pc-windows-msvc
```

## Pre-tag checklist

- [ ] `cargo build --release` on the host OS
- [ ] `mako --version` matches `Cargo.toml`
- [ ] `mako doctor` passes on a clean installed prefix
- [ ] `mako update --from . --prefix <tmp>` refreshes a test prefix
- [ ] `scripts/package-release.*` artifact includes runtime, stdlib, docs, editor scaffold, install/uninstall scripts, checksums
- [ ] Unpacked artifact installs with `scripts/install.* --skip-build` and installed `mako doctor` passes
- [ ] `scripts/install-release.sh --base-url file://<dist> --artifact <name>` verifies checksum and installs
- [ ] Release workflow install-smokes each OS artifact before uploading/publishing assets
- [ ] `mako test examples/testing` → all pass (no live env)
- [ ] Spot-check: `mako init /tmp/demo && mako run /tmp/demo/main.mko`
- [ ] Optional: one cross smoke (`--target x86_64-pc-windows-gnu` with zig)
- [ ] CHANGELOG.md updated

## Optional live (not required for green CI)

```bash
MAKO_LIVE_TLS=1 mako test examples/testing/tls_live_test.mko -v
MAKO_LIVE_NGHTTP2=1 mako test examples/testing/tls_live_test.mko -r Nghttp2 -v
MAKO_LIVE_QUIC=1 mako test examples/testing/quiche_link_test.mko -v
```

## homebrew-core (external — user action)

See prior notes in STATUS / Formula. Agent cannot complete org ownership / core review.

## Honesty / gaps

- **Darwin cross from Linux** needs a macOS SDK; zig alone is not always enough for framework-linked optional deps.
- **Windows MSVC** host: use LLVM clang; MinGW/`x86_64-pc-windows-gnu` is the easy cross target via zig.
- Optional libs absent ⇒ builtins soft-fail / tests early-return.
- Runtime headers must remain discoverable for user compiles.
