# WASM / WASI (preview1 beachhead + browser/edge starter)

Book: [§12 Cross-platform & WASI](book/src/ch12-cross-platform.md) · How-to: [howto/07-wasi.md](howto/07-wasi.md).

**Product tip:** **0.2.1**. Deeper browser/DOM product is **0.3.0** on the roadmap.

## Status

`mako build --target wasm32-wasi` (alias of **`wasm32-wasip1`**) uses **wasi-sdk**
clang + sysroot when `WASI_SDK_PATH` (or `/opt/wasi-sdk`, `/usr/local/wasi-sdk`) is set.

| Piece | Status |
|-------|--------|
| Driver: wasi-sdk clang, `--target=wasm32-wasip1`, no host `-pthread`/OpenSSL | Done |
| Minimal runtime (`-DMAKO_WASI` → `mako_rt.h` only) | Done |
| Hello / print + fib → runnable `.wasm` under **wasmtime** | Done |
| argv / environ (`argc` / `arg_get` / `env_get` via wasi-libc) | Done |
| **FS preopens** (`read_file` / `write_file` + wasmtime `--dir`) | Done |
| Clear skip when SDK / wasmtime missing | Done (`scripts/wasi-verify.sh`) |
| Sockets / HTTP / TLS / DB on WASI | VISION Later |
| WASI preview2 / full browser DOM | Target / later |
| Browser/edge starter (`mako deploy wasm`) | Done for preview1 loader path |
| Browser loader polyfill (`wasm/mako-wasi-loader.js`) | Seed |

STATUS counts preview1 beachhead as **Done**. `mako deploy wasm` makes that
path usable in browser/edge-style static hosting through a preview1 polyfill.
Sockets, preview2 components, Workers request adapters, and full browser DOM
bindings remain target work.

`env_set` soft-fails on WASI (no `setenv` in wasi-libc) — pass env from the host
(`wasmtime --env KEY=VAL`). Browser loader supplies empty argv/environ.

**FS paths:** use relative names with `--dir=HOST::.` (guest `.` = sandbox), or
absolute `/file` with `--dir=HOST::/`. Without a matching preopen, `read_file`
returns `""` and `write_file` returns `-1`.

## Try (local SDK)

```bash
export WASI_SDK_PATH=/path/to/wasi-sdk   # or use .mako/toolchains/wasi-sdk
mako build examples/wasi_hello.mko --target wasm32-wasi -o out/wasi_hello.wasm
wasmtime out/wasi_hello.wasm
# → hello from mako wasi / 55

mako build examples/wasi_args_env.mko --target wasm32-wasi -o out/wasi_args_env.wasm
wasmtime --env MAKO_WASI_GREET=hi out/wasi_args_env.wasm hello
# → argc / hello / hi

mkdir -p out/wasi_fs_sandbox && echo seed > out/wasi_fs_sandbox/in.txt
mako build examples/wasi_fs.mko --target wasm32-wasi -o out/wasi_fs.wasm
wasmtime --dir=out/wasi_fs_sandbox::. out/wasi_fs.wasm
# → seed / 0 / wrote
```

Or: `./scripts/wasi-verify.sh` (exits 0 with `skip:` if toolchain missing).

## Emit C then cross-compile

```bash
mako build examples/wasi_hello.mko --emit-c -o /tmp/wasi_hello.c
# Generated C still has `#ifndef MAKO_WASI` guards; for manual clang:
$WASI_SDK_PATH/bin/clang --target=wasm32-wasip1 \
  --sysroot=$WASI_SDK_PATH/share/wasi-sysroot \
  -I runtime -O2 -std=gnu11 -DMAKO_WASI -D_WASI_EMULATED_PROCESS_CLOCKS \
  -D_POSIX_C_SOURCE=200809L /tmp/wasi_hello.c -o /tmp/wasi_hello.wasm \
  -lwasi-emulated-process-clocks
```

## Docker recipe (no local SDK)

```bash
./scripts/wasi-ci-build.sh
# or:
docker build -f docker/wasi-build.Dockerfile -t mako-wasi .
```

`docker/wasi-build.Dockerfile` installs wasi-sdk, builds mako, then runs
`mako build examples/hello.mko --target wasm32-wasi` and checks the `.wasm` is non-empty.

## Browser glue (fd_write + empty environ/args)

Generate a starter:

```bash
mako deploy wasm wasm-dist --entry examples/wasi_hello.mko --wasm hello.wasm --port 8080
./wasm-dist/build-wasm.sh
python3 -m http.server -d wasm-dist 8080
```

`wasm/mako-wasi-loader.js` + `wasm/index.html` fetch `hello.wasm` and instantiate
with a minimal `wasi_snapshot_preview1` import object:

- **`fd_write`** → `console.log` / page `<pre>`
- **`environ_sizes_get` / `environ_get`** → empty environ (count 0; NULL list)
- **`args_sizes_get` / `args_get`** → empty argv (argc 0; NULL list)
- **`clock_time_get`** → `Date.now()` as realtime nanoseconds
- **`random_get`** → `crypto.getRandomValues` (fallback `Math.random`)
- **`fd_prestat_get` / `fd_prestat_dir_name`** → one virtual preopen: fd **3** → `"/"`; other fds → `EBADF` (8)
- **`path_open`** under fd 3: `hello.txt` → `"hi"`, `bye.txt` → `"bye"`; unknown without `O_CREAT` → `ENOENT` (44); **`O_CREAT`** → empty writable virtual file; other dirfds → `ENOTCAPABLE` (76)
- **`path_open` `/host/<rel>`** → fetch cwd-relative `./<rel>` (sync XHR). With `FD_WRITE` rights: in-memory write overlay (`HOST_OVERLAY`); CREAT allowed for empty overlay. Rejects `..`, absolute paths. **Not** a general host FS — overlay never escapes the browser.
- **`path_create_directory`** → `ENOTCAPABLE` (76)
- **`path_unlink_file`** under fd 3 → remove virtual path (`ENOENT` if missing)
- **`fd_seek` / `fd_tell`** on virtual file fds (whence SET/CUR/END)
- **`fd_filestat_get`** → filetype regular + `size` for virtual file fds
- **`fd_read` / `fd_write`** → virtual file bytes; writable fds append via `fd_write` and are readable back

Other WASI calls are unsupported and return 0 / `ENOSYS`.

```bash
./scripts/wasi-ci-build.sh
cp out/hello.wasm wasm/
python3 -m http.server -d wasm 8080
# open http://127.0.0.1:8080/
```

## Preview2 / edge boundary

`mako deploy wasm` is the browser/edge story for current Mako: build a WASI
preview1 module and run it behind a JS polyfill. It is useful for CLIs,
deterministic compute, demos, and static-hosted edge experiments.

Still target work:

- WASI preview2/component-model output
- WIT interface generation
- HTTP sockets and request adapters for Workers/Lambda@Edge-style platforms
- Full browser DOM bindings
- Full filesystem semantics (readdir, real mkdir/rights, arbitrary host paths)
