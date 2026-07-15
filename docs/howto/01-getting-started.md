# Getting Started

This guide walks you through installing Mako, creating a project, and running
your first program. By the end you will have a working development loop.

## Install Mako

From a source checkout (requires cargo/rustc and clang):

```bash
make install
```

This places the `mako` binary in `~/.local/bin/` and runtime headers in
`~/.local/share/mako/runtime`. Ensure `~/.local/bin` is on your PATH.

Verify the installation:

```bash
mako version
# mako version mako0.1.5 darwin/arm64

mako version -v
# includes the git commit hash
```

## Create a project

Mako provides scaffolding for three project shapes:

```bash
# Simple application
mako init hello --name hello

# Backend API service (includes HTTP handler scaffold)
mako init mysvc --backend

# Multi-package workspace (lib + app)
mako init myws --workspace
```

Each creates a directory with `mako.toml` and a `main.mko` entry point.

## Project structure

After `mako init hello --name hello`:

```
hello/
  mako.toml        # package manifest (name, version, dependencies)
  main.mko         # entry point — must contain func main() / fn main()
```

The `mako.toml` looks like:

```toml
name = "hello"
version = "0.1.0"
```

## Write your first program

Open `hello/main.mko` (Mako-native syntax preferred):

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

Identity guide: [IDENTITY.md](../IDENTITY.md).  
Dual forms (`func`, `:=`, …) still work for compatibility.

## The development loop

From inside the `hello/` directory:

```bash
# Typecheck without compiling (fast feedback)
mako check main.mko

# Compile and run in one step
mako run main.mko

# Build a binary (name comes from mako.toml)
mako build main.mko

# Run the binary directly
./hello
```

## Passing arguments to your program

```bash
mako run main.mko -- arg1 arg2
```

Inside the program, use `argc()`, `arg_get(i)`, or `args()` to read them.

## Key commands

| Command | Purpose |
|---------|---------|
| `mako check file.mko` | Typecheck (incremental, fast) |
| `mako run file.mko` | Compile and execute |
| `mako build file.mko` | Produce a binary |
| `mako build --release file.mko` | Optimized production binary |
| `mako build -j 8 file.mko` | Parallel compilation |
| `mako test path/` | Run tests |
| `mako fmt file.mko` | Format source code |
| `mako version` | Print version and platform |

## Running from source (no install)

If you have not installed yet, run directly from the compiler source tree:

```bash
cargo run --release -- check examples/hello.mko
cargo run --release -- run examples/hello.mko
```

## Environment variables

| Variable | Purpose |
|----------|---------|
| `MAKO_RUNTIME` | Override runtime header location |
| `MAKO_JOBS` | Default parallel job count (same as `-j`) |

## Next steps

- [Build an HTTP API](02-http-apis.md)
- [Handle errors properly](03-errors-debugging.md)
- [Set up packages and dependencies](04-packages.md)
