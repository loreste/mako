# 12. Cross-Platform and WASI

Mako produces native binaries for multiple operating systems and architectures
from a single source tree. This chapter covers cross-compilation, the supported
target matrix, WebAssembly output, and static linking.

---

## The `--target` Flag

By default, `mako build` produces a binary for the host machine. To
cross-compile, pass a target triple:

```bash
mako build main.mko --target x86_64-unknown-linux-gnu -o bin/app-linux
mako build main.mko --target aarch64-apple-darwin -o bin/app-mac
mako build main.mko --target x86_64-pc-windows-msvc -o bin/app.exe
mako build main.mko --target wasm32-wasip1 -o bin/app.wasm
```

The target triple follows the `<arch>-<vendor>-<os>[-<abi>]` convention.

---

## Supported Targets

| Target Triple                   | OS      | Architecture | Notes                         |
|---------------------------------|---------|--------------|-------------------------------|
| `aarch64-apple-darwin`          | macOS   | ARM64        | Apple Silicon native          |
| `x86_64-apple-darwin`           | macOS   | x86-64       | Intel Macs                    |
| `x86_64-unknown-linux-gnu`      | Linux   | x86-64       | glibc (default on most distros) |
| `x86_64-unknown-linux-musl`     | Linux   | x86-64       | Static musl binary            |
| `aarch64-unknown-linux-gnu`     | Linux   | ARM64        | Graviton, Ampere, RPi4        |
| `aarch64-unknown-linux-musl`    | Linux   | ARM64        | Static ARM64                  |
| `x86_64-pc-windows-msvc`        | Windows | x86-64       | MSVC ABI (needs clang on PATH)|
| `wasm32-wasip1`                 | WASI    | WebAssembly  | Preview 1 (see below)         |

The host target is detected automatically. `mako version` prints it:

```text
mako version mako0.1.8 darwin/arm64
mako version mako0.1.8 linux/amd64
```

---

## Using Zig as the C Compiler

When `zig` is installed and available on PATH, Mako can use `zig cc` as the C
backend instead of system clang. This is particularly useful for cross-compilation
because zig bundles sysroots for many targets:

```bash
# Cross-compile to Linux x86-64 from macOS using zig
MAKO_CC="zig cc" mako build main.mko --target x86_64-unknown-linux-gnu -o bin/app-linux

# Cross-compile to Linux ARM64
MAKO_CC="zig cc" mako build main.mko --target aarch64-unknown-linux-gnu -o bin/app-arm64

# Cross-compile to Linux musl (static) from macOS
MAKO_CC="zig cc" mako build main.mko --target x86_64-unknown-linux-musl -o bin/app-static
```

The `MAKO_CC` environment variable overrides the C compiler used by the backend.
Set it to `zig cc` to get zig's cross-compilation sysroots.

### Why Zig CC

- Ships sysroots for Linux glibc (many versions), musl, and Windows.
- No separate toolchain installation per target.
- Single binary, no dependencies.
- Drop-in replacement for clang in most cases.

### When to Use System Clang

- Building for the **host** platform (fastest, no setup needed).
- macOS targets (Apple SDK headers are needed, which zig does not bundle).
- When you need specific clang flags or sanitizers not supported through zig.

---

## Static Linking with Musl

Linux musl targets produce fully static binaries with no runtime dependencies:

```bash
mako build main.mko --target x86_64-unknown-linux-musl -o bin/app
file bin/app
# bin/app: ELF 64-bit LSB executable, x86-64, statically linked
```

Musl targets default to `--static-link`. The resulting binary runs on any Linux
kernel of the right architecture, regardless of the distribution or installed
libraries.

You can also force static linking on glibc targets:

```bash
mako build main.mko --static-link --target x86_64-unknown-linux-gnu -o bin/app
```

And disable it when you want dynamic linking:

```bash
mako build main.mko --no-static-link --target x86_64-unknown-linux-musl -o bin/app
```

### Static Linking Matrix

| Target              | Default     | Override Available |
|---------------------|-------------|-------------------|
| Linux musl          | Static      | `--no-static-link` |
| Linux glibc         | Dynamic     | `--static-link`    |
| macOS (darwin)      | Dynamic     | Not supported      |
| Windows (msvc)      | Dynamic     | Not supported      |
| WASM                | N/A         | N/A                |

---

## WASI / WebAssembly Compilation

Mako can compile programs to WebAssembly targeting the WASI (WebAssembly System
Interface) preview 1 specification. This produces `.wasm` modules that run in
any WASI-compatible runtime.

### Prerequisites

Install the **wasi-sdk** and set `WASI_SDK_PATH`:

```bash
export WASI_SDK_PATH=/opt/wasi-sdk
```

Or install it to a common path (`/opt/wasi-sdk`, `/usr/local/wasi-sdk`) and Mako
will find it automatically.

You also need a WASI runtime to execute the output. **wasmtime** is recommended:

```bash
# Install wasmtime (example for macOS/Linux)
curl https://wasmtime.dev/install.sh -sSf | bash
```

### Building for WASI

```bash
mako build examples/wasi_hello.mko --target wasm32-wasi -o out/wasi_hello.wasm
```

The target `wasm32-wasi` is normalized to `wasm32-wasip1` internally. Both forms
are accepted.

### Running with Wasmtime

```bash
wasmtime out/wasi_hello.wasm
# Output: hello from mako wasi
#         55
```

### Passing Arguments and Environment

WASI programs can receive command-line arguments and environment variables from
the host:

```mko
// wasi_args_env.mko
fn main() {
    print_int(argc())
    let a = args()
    for i in range a {
        print(a[i])
    }
    let greeting = env_get("MAKO_WASI_GREET")
    print(greeting)
}
```

```bash
mako build wasi_args_env.mko --target wasm32-wasi -o out/wasi_args_env.wasm
wasmtime --env MAKO_WASI_GREET=hello out/wasi_args_env.wasm world
# Output:
# 2
# out/wasi_args_env.wasm
# world
# hello
```

### File System Access with Preopens

WASI sandboxes file system access. You must grant directory access with
`--dir`:

```mko
// wasi_fs.mko
fn main() {
    let content = read_file("./in.txt")
    print(content)
    let _ = write_file("./out.txt", "written from wasm")
    print("fs done")
}
```

```bash
mkdir -p out/wasi_sandbox && echo "seed data" > out/wasi_sandbox/in.txt
mako build wasi_fs.mko --target wasm32-wasi -o out/wasi_fs.wasm
wasmtime --dir=out/wasi_sandbox::. out/wasi_fs.wasm
# Output:
# seed data
# fs done
```

The `--dir=host_path::guest_path` syntax maps a host directory to a guest path.

### What Works in WASI Builds

The WASI runtime (`-DMAKO_WASI`) supports:

| Feature           | Status      |
|-------------------|-------------|
| `print` / `print_int` | Works  |
| `argc` / `arg_get` / `args` | Works |
| `env_get`         | Works       |
| `env_set`         | Soft-fails (no-op) |
| `read_file` / `write_file` | Works (with preopens) |
| Arithmetic, slices, structs | Works |
| `fn fib(n)` and all pure computation | Works |
| TCP / HTTP / TLS  | Not available (native-only) |
| SQLite / Postgres / Redis | Not available (native-only) |
| Channels / crew   | Not available (native-only) |

### Browser/Edge Deployment

For deploying WASI modules to the browser or edge platforms:

```bash
mako deploy wasm wasm-dist --entry examples/wasi_hello.mko --wasm hello.wasm
```

This generates:
- `build-wasm.sh` -- script to compile the `.wasm` file
- `index.html` -- minimal HTML loader page
- `mako-wasi-loader.js` -- JavaScript WASI preview1 shim
- `README.md` -- deployment instructions

Then:

```bash
cd wasm-dist
./build-wasm.sh
python3 -m http.server 8080
# Open http://localhost:8080 in a browser
```

### Verifying WASI Builds in CI

The script `./scripts/wasi-verify.sh` checks that the wasi-sdk and wasmtime are
available, builds the WASI examples, and runs them. If the toolchain is missing,
it prints `skip:` and exits 0 so CI does not fail:

```bash
./scripts/wasi-verify.sh
```

---

## Windows Targets

On Windows, install LLVM/clang and ensure it is on PATH. The install script is:

```powershell
.\scripts\install.ps1
```

Build as usual:

```bash
mako build main.mko --target x86_64-pc-windows-msvc -o bin\app.exe
```

Or cross-compile from Linux/macOS using zig:

```bash
MAKO_CC="zig cc" mako build main.mko --target x86_64-pc-windows-msvc -o bin/app.exe
```

---

## `mako version` Output

The `version` command reports the Mako version, OS, and architecture:

```bash
mako version
# mako version mako0.1.8 darwin/arm64

mako --version
# mako version mako0.1.8 darwin/arm64

mako -V
# mako version mako0.1.8 darwin/arm64
```

For verbose output including the git commit:

```bash
mako version -v
# mako version mako0.1.8 darwin/arm64
# commit: a1b2c3d (when built from git with MAKO_GIT_HASH)
```

---

## Environment Variables for Cross-Compilation

| Variable         | Purpose                                              |
|------------------|------------------------------------------------------|
| `MAKO_CC`        | Override the C compiler (e.g., `zig cc`, `clang-17`) |
| `MAKO_RUNTIME`   | Override the runtime header/source path              |
| `WASI_SDK_PATH`  | Path to wasi-sdk installation                        |
| `MAKO_JOBS`      | Parallel compilation jobs                            |
| `MAKO_CACHE`     | Cache directory for incremental builds               |

---

## Docker Deployment with Static Binaries

A common pattern is building a static Linux binary and deploying it in a minimal
Docker image:

```bash
# Build a static musl binary
MAKO_CC="zig cc" mako build main.mko --target x86_64-unknown-linux-musl --release -o bin/server

# Or use the deploy command to generate a Dockerfile
mako deploy docker . --entry main.mko --bin server --port 8080
```

The generated Dockerfile uses a multi-stage build: compile in a full image, copy
the static binary into `scratch` (empty image). The resulting container is
typically under 5 MB.

For applications that need OS trust stores or debugging tools:

```bash
mako deploy docker . --entry main.mko --bin server --port 8080 --mode debian
```

This uses `debian:bookworm-slim` as the runtime base instead of `scratch`.

---

## Putting It All Together

A typical CI matrix for a Mako service:

```bash
# macOS ARM64 (native)
mako build --release main.mko -o dist/app-darwin-arm64

# Linux x86-64 (static)
MAKO_CC="zig cc" mako build --release main.mko \
    --target x86_64-unknown-linux-musl -o dist/app-linux-amd64

# Linux ARM64 (static)
MAKO_CC="zig cc" mako build --release main.mko \
    --target aarch64-unknown-linux-musl -o dist/app-linux-arm64

# WASI (portable)
mako build main.mko --target wasm32-wasip1 -o dist/app.wasm

# Verify
file dist/app-darwin-arm64   # Mach-O 64-bit executable arm64
file dist/app-linux-amd64    # ELF 64-bit, statically linked
file dist/app-linux-arm64    # ELF 64-bit, aarch64, statically linked
file dist/app.wasm           # WebAssembly (wasm)
```

---

## What Is Not Yet Supported

The following are on the roadmap but not shipped:

- **WASI preview2** (component model)
- **Browser DOM bindings**
- **WASI sockets** (networking in WASM)
- **iOS / Android** targets

See the VISION document for the long-term target plan.

Next: [Tooling](ch13-tooling.md).
