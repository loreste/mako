# 2. Getting Started

## Install

**macOS / Linux**

```bash
make install                    # → ~/.local/bin/mako + ~/.local/share/mako/runtime
# or: ./scripts/install.sh
mako version                    # → mako version mako0.1.0 darwin/arm64
mako --version                  # same info
mako version -v                 # optional commit line when available
```

**Windows (PowerShell)**

```powershell
cargo build --release
.\scripts\install.ps1
mako version
```

You need **cargo/rustc** (to build the compiler) and **clang** (Xcode / apt / LLVM).
Optional: OpenSSL, libnghttp2, SQLite, libpq, quiche. Headers live under
`$PREFIX/share/mako/runtime` (`MAKO_RUNTIME` overrides).

From a checkout without installing:

```bash
cargo run --release -- version
cargo run --release -- run examples/hello.mko
```

## Hello

```bash
mako init hello --name hello
cd hello
mako run main.mko
mako check main.mko
mako build main.mko             # binary named from mako.toml when file is main.mko
```

A minimal program:

```mko
fn main() {
    print("hello from mako")
}
```

The repo’s `examples/hello.mko` also prints `fib(10)`:

```mko
fn main() {
    print("hello from mako")
    print_int(fib(10))
}

fn fib(n: int) -> int {
    if n <= 1 {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}
```

```bash
mako run examples/hello.mko
# hello from mako
# 55
```

## Scaffolds

```bash
mako init mysvc --backend       # HTTP-oriented service layout
cd mysvc && mako run main.mko

mako init myws --workspace      # lib + app members
cd myws && mako check . && mako run -p app
```

## Edit loop

| Command | Use |
|---------|-----|
| `mako version` | Version + OS/arch |
| `mako check` | Fast typecheck (incremental) |
| `mako run` | Compile + execute |
| `mako build -j 8` | Parallel object compile |
| `mako build --release` | `-O3 -flto` production binary |
| `mako test examples/testing` | Full suite |

Incremental cache is **on by default** — see [BUILD.md](../../BUILD.md).

Next: [Language Tour](ch03-language-tour.md).
