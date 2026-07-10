# Getting started

## Install

```bash
make install          # ~/.local/bin/mako + runtime headers
# or: ./scripts/install.sh
mako version          # mako version mako0.1.0 darwin/arm64
mako --version        # same info
```

Needs **cargo/rustc** (to build the compiler) and **clang**. Optional: OpenSSL, libnghttp2, SQLite, libpq.

## First program

```bash
mako init hello --name hello
cd hello
mako run main.mko
mako check main.mko
mako build main.mko          # binary named from mako.toml
```

Backend API scaffold:

```bash
mako init mysvc --backend
cd mysvc && mako run main.mko
```

Workspace (lib + app):

```bash
mako init myws --workspace
cd myws && mako check . && mako run -p app
```

## Edit loop

| Command | Use |
|---------|-----|
| `mako version` | Version + OS/arch (`mako version -v` for commit) |
| `mako check` | Fast typecheck (incremental) |
| `mako run` | Compile + execute |
| `mako build -j 8` | Parallel object compile |
| `mako build --release` | `-O3 -flto` production binary |

Incremental cache: [BUILD.md](../BUILD.md). Book: [Getting Started chapter](../book/src/ch02-getting-started.md).
Next: [HTTP APIs](02-http-apis.md).
