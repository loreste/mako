# 12. Cross-platform & WASI

## Native targets

```bash
mako build main.mko --target <triple>
```

When **zig cc** is available, cross-compile uses it automatically. Release /
OS matrix: [RELEASE.md](../../RELEASE.md). Windows install: `scripts/install.ps1`
(LLVM clang on PATH).

`mako version` prints OS/arch:

```text
mako version mako0.1.0 darwin/arm64
mako version mako0.1.0 linux/amd64
```

## WASI preview1 (Done)

With **wasi-sdk** (`WASI_SDK_PATH` or common install paths):

```bash
mako build examples/wasi_hello.mko --target wasm32-wasi -o out/wasi_hello.wasm
wasmtime out/wasi_hello.wasm
# → hello from mako wasi / 55

mako build examples/wasi_args_env.mko --target wasm32-wasi -o out/wasi_args_env.wasm
wasmtime --env MAKO_WASI_GREET=hi out/wasi_args_env.wasm hello

mkdir -p out/wasi_fs_sandbox && echo seed > out/wasi_fs_sandbox/in.txt
mako build examples/wasi_fs.mko --target wasm32-wasi -o out/wasi_fs.wasm
wasmtime --dir=out/wasi_fs_sandbox::. out/wasi_fs.wasm
```

`wasm32-wasi` normalizes to **`wasm32-wasip1`**. Runtime is minimal
(`-DMAKO_WASI`): print, fib, argv/env, file read/write with preopens.
Sockets / TLS / DB stay **native-only**. Missing SDK →
`./scripts/wasi-verify.sh` prints `skip:` and exits 0.

Full notes: [WASM.md](../../WASM.md) · how-to: [howto/07-wasi.md](../../howto/07-wasi.md).

## Later (VISION)

WASI preview2, browser DOM, WASI sockets — not claimed Done. See
[VISION.md](../../VISION.md).

Next: [Tooling](ch13-tooling.md).
