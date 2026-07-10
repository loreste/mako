# Compiling to WebAssembly (WASI)

Mako can compile programs to WebAssembly using WASI preview1. This lets you
run Mako programs in sandboxed environments, edge runtimes, and browsers.

## Prerequisites

Install wasi-sdk and wasmtime:

```bash
# wasi-sdk (provides the WASI clang toolchain)
# Download from https://github.com/WebAssembly/wasi-sdk/releases
# Set the environment variable:
export WASI_SDK_PATH=/path/to/wasi-sdk

# wasmtime (WebAssembly runtime)
curl https://wasmtime.dev/install.sh -sSf | bash
```

## Hello World in WASM

Write a simple program:

```mko
// wasi_hello.mko
fn main() {
    print("hello from mako wasm")
}
```

Build and run:

```bash
mako build wasi_hello.mko --target wasm32-wasi -o hello.wasm
wasmtime hello.wasm
# hello from mako wasm
```

The `--target wasm32-wasi` flag selects the WASI preview1 backend. Mako
normalizes this to `wasm32-wasip1` internally.

## Command-line arguments

WASI programs can receive arguments from the host:

```mko
// wasi_args.mko
fn main() {
    print_int(argc())
    for i in range argc() {
        print(arg_get(i))
    }
}
```

```bash
mako build wasi_args.mko --target wasm32-wasi -o args.wasm
wasmtime args.wasm -- hello world
# 3
# args.wasm
# hello
# world
```

## Environment variables

Access host environment variables (passed explicitly to wasmtime):

```mko
// wasi_env.mko
fn main() {
    let greeting = env_get("MAKO_GREET")
    if str_eq(greeting, "") {
        print("no greeting set")
    } else {
        print(greeting)
    }
}
```

```bash
mako build wasi_env.mko --target wasm32-wasi -o env.wasm
wasmtime --env MAKO_GREET=hi env.wasm
# hi
```

Note: `env_set` is a no-op on WASI (the sandbox does not allow modifying the
environment).

## File system access

WASI sandboxes file access. You must grant directory preopens:

```mko
// wasi_fs.mko
fn main() {
    let _ = write_file("output.txt", "written from wasm")
    let content = read_file("output.txt")
    print(content)
}
```

```bash
mkdir -p sandbox
mako build wasi_fs.mko --target wasm32-wasi -o fs.wasm
wasmtime --dir=sandbox::. fs.wasm
# written from wasm
cat sandbox/output.txt
# written from wasm
```

The `--dir=sandbox::.` flag maps the host directory `sandbox/` to the guest
path `.` (current directory inside the WASM module).

## What works on WASI

| Feature | Status |
|---------|--------|
| `print` / `print_int` | Works |
| `argc` / `arg_get` / `args` | Works |
| `env_get` | Works |
| `read_file` / `write_file` | Works (with preopens) |
| `file_exists` | Works (with preopens) |
| Arithmetic, control flow, structs | Works |
| Result types, match, enums | Works |

## What stays native-only

These features require OS capabilities not available in WASI preview1:

| Feature | Reason |
|---------|--------|
| Networking (TCP, HTTP) | No socket support in preview1 |
| TLS / HTTPS | Requires OpenSSL |
| SQLite / Postgres / Redis | Requires native libraries |
| `crew` / channels | No thread support in preview1 |
| `arena` (advanced) | Limited memory model |

## Browser deployment

Generate a browser-ready static site with the WASI loader:

```bash
mako deploy wasm dist/ --entry wasi_hello.mko --wasm hello.wasm
```

This creates:

```
dist/
  index.html           # Loads and runs the WASM module
  mako-wasi-loader.js  # WASI preview1 polyfill
  build-wasm.sh        # Rebuild script
  README.md            # Usage instructions
```

Build and serve:

```bash
cd dist
./build-wasm.sh
python3 -m http.server 8080
# Open http://localhost:8080 in a browser
```

## Verifying your setup

Run the verification script to check that wasi-sdk and wasmtime are configured:

```bash
./scripts/wasi-verify.sh
```

If dependencies are missing, it prints `skip:` and exits cleanly (useful in CI).

## Complete example

```mko
// wasi_demo.mko
fn fib(n: int) -> int {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}

fn main() {
    print("WASI Fibonacci demo")
    let n = 10
    print_int(fib(n))

    let name = env_get("USER")
    if not str_eq(name, "") {
        print("hello, " + name)
    }
}
```

```bash
mako build wasi_demo.mko --target wasm32-wasi -o demo.wasm
wasmtime --env USER=mako demo.wasm
# WASI Fibonacci demo
# 55
# hello, mako
```

## Next steps

- [Testing](08-testing.md)
- [Release builds](09-release-builds.md) (native optimized binaries)
- [WASM.md](../WASM.md) for full technical details
