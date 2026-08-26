# 2. Getting Started

This chapter walks you through installing Mako, creating your first project,
understanding the project structure, and setting up your editor.

## System requirements

To build and run Makori programs you need:

- **macOS**: nothing — the release binary is fully self-contained (bundled linker)
- **Linux**: `gcc` or `clang` for linking (`apt install gcc` or `apt install clang`)
- **Windows**: LLVM clang on PATH
- **A POSIX-like shell** (bash/zsh on macOS/Linux; PowerShell on Windows)

Optional dependencies for full standard library support:

- OpenSSL (TLS)
- libnghttp2 (HTTP/2)
- SQLite (database)
- libpq (PostgreSQL)

## Installing Mako

### macOS and Linux (recommended)

From a source checkout:

```bash
make install
```

This installs the `mako` binary to `~/.local/bin/mako` and runtime headers to
`~/.local/share/mako/runtime`. Make sure `~/.local/bin` is in your `PATH`.

Alternatively, use the install script directly:

```bash
./scripts/install.sh
```

Verify the installation:

```bash
mako version
# mako version mako0.4.0 darwin/arm64
```

The `--version` flag produces the same output:

```bash
mako --version
# mako version mako0.4.0 darwin/arm64
```

For verbose output including the git commit (when available):

```bash
mako version -v
```

### Building from source

If you want to build from the repository:

```bash
git clone https://github.com/mako-lang/mako.git
cd mako
cargo build --release
```

You can run the compiler directly without installing:

```bash
cargo run --release -- version
cargo run --release -- run examples/hello.mko
```

Then install when ready:

```bash
make install
```

### Windows (PowerShell)

```powershell
cargo build --release
.\scripts\install.ps1
mako version
```

On Windows, clang must be on PATH. Install via `choco install llvm` or download
from the [LLVM releases page](https://llvm.org/). On macOS no external tools are
needed — the release binary ships with a bundled linker.

### Runtime path override

The compiler looks for runtime headers at `$PREFIX/share/mako/runtime`. If you
installed to a non-standard location, set the `MAKO_RUNTIME` environment
variable:

```bash
export MAKO_RUNTIME=/opt/mako/runtime
```

## The mako doctor command

After installation, run `makori doctor` to verify your environment is correctly
configured:

```bash
mako doctor
```

This checks:

- The `mako` binary is in PATH and executable
- System linker is available (Linux/Windows; not needed on macOS)
- Runtime headers are found at the expected path
- Optional dependencies (OpenSSL, SQLite, etc.) are detected
- The standard library path resolves correctly

If anything is misconfigured, `makori doctor` prints actionable guidance on how
to fix it.

## Your first program

Create a file called `hello.mko`:

```mko
fn main() {
    print("hello from mako")
}
```

Run it:

```bash
mako run hello.mko
# hello from mako
```

That is the entire workflow. `makori run` compiles the source to a native binary
and executes it in one step.

## Creating a project with mako init

For anything beyond a single file, use `makori init` to scaffold a project:

```bash
mako init myapp --name myapp
cd myapp
```

This creates:

```
myapp/
  mako.toml      -- project manifest
  main.mko       -- entry point
```

Run the generated project:

```bash
mako run main.mko
```

### Backend service scaffold

For an HTTP-oriented service layout:

```bash
mako init mysvc --backend
cd mysvc
mako run main.mko
```

This generates a project with HTTP server boilerplate and route handlers.

### Workspace scaffold

For a project with multiple members (library + application):

```bash
mako init myws --workspace
cd myws
mako check .
mako run -p app
```

## Project structure: mako.toml

The `mako.toml` file is the project manifest. It declares the project name,
version, dependencies, and build configuration:

```toml
[package]
name = "myapp"
version = "0.1.0"

[dependencies]
# path dependencies
utils = { path = "../utils" }

# registry dependencies (when available)
# json = "1.0"

[build]
# parallel compilation jobs
jobs = 8
```

When you run `makori build main.mko` in a directory with a `mako.toml`, the
binary name is derived from the package name.

## The build and run cycle

| Command | What it does |
|---------|--------------|
| `makori run file.mko` | Compile and execute in one step |
| `makori check file.mko` | Type-check without producing a binary (fast) |
| `makori build file.mko` | Compile to a native binary |
| `makori build --release file.mko` | Optimized build (`-O3 -flto`) |
| `makori build -j 8 file.mko` | Parallel object compilation |
| `makori test examples/testing` | Run the test suite |
| `makori fmt file.mko` | Format source to canonical style |

### Incremental compilation

Incremental compilation is on by default. The compiler caches intermediate
artifacts and only recompiles translation units that have changed. This makes
the edit-compile-run loop fast even for larger projects.

### Release builds

For production deployment, always use `--release`:

```bash
mako build --release main.mko
```

This enables `-O3` optimization and link-time optimization (`-flto`), producing
a smaller, faster binary. Static linking depends on the target and toolchain;
Linux musl is the documented static path, while glibc and platform libraries
may remain dynamic. See [Cross-platform builds](ch12-cross-platform.md).

## A more complete first program

Here is a slightly more involved example showing functions, types, and control
flow:

```mko
fn main() {
    print("Fibonacci calculator")
    let n = 10
    print_int(fib(n))

    let mut sum = 0
    for i in range n {
        sum = sum + fib(i)
    }
    print_int(sum)
}

fn fib(n: int) -> int {
    if n <= 1 {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}
```

```bash
mako run fib.mko
# Fibonacci calculator
# 55
# 88
```

## Working with multiple files

Most projects need more than one file. Makori uses **packs** and **pulls**:
name a unit with `pack`, bring it in with `pull`, and always call through the
pack name. When you `makori run main.mko`, the compiler pulls everything in.

### Basic file pull

Start with two files side by side:

```mko
// lib.mko
pack lib

fn add(a: int, b: int) -> int {
    return a + b
}

fn greet(name: string) -> string {
    return "hi " + name
}
```

```mko
// main.mko
pull "./lib.mko"

fn main() {
    print_int(lib.add(2, 3))
    print(lib.greet("mako"))
}
```

```bash
mako run main.mko
# 5
# hi mako
```

The default qualifier is the pulled file’s `pack` name (if not `main`),
otherwise the path basename. Symbols are **not** merged bare into the importer.

### Explicit aliases

```mko
// main.mko
pull "./lib.mko" as lib
// dual: import lib "./lib.mko"

fn main() {
    print_int(lib.add(2, 3))
}
```

Also available: blank `pull _ "fmt"` (load only) and dot `pull . "./h.mko"`
(bare names — specialized; use sparingly).

### Growing into a multi-file project

Say you're building a small service. Start with `makori init`, then add files
as you go:

```bash
mako init myservice --name myservice
cd myservice
```

```
myservice/
  mako.toml
  main.mko
  routes.mko       # you add this
  db.mko           # you add this
```

```mko
// db.mko
pack db

fn init() {
    print("database ready")
}

fn count() -> int {
    return 42
}
```

```mko
// routes.mko
pack routes

fn health(c: int) {
    let _ = http_respond_json(c, 200, "{\"ok\":true}")
}
```

```mko
// main.mko
pull "./routes.mko"
pull "./db.mko"

fn main() {
    db.init()
    let fd = http_bind(8080)
    print("listening on :8080")
    // handle requests with routes.health(c) ...
}
```

### Grouped pulls

When you have several pulls, group them into a single block:

```mko
pull (
    "strings"
    "./db.mko" as db
    "./routes.mko" as routes
)
```

The formatter (`makori fmt`) rewrites multiple `pull` lines into this grouped form.

### Standard library

Pull std units by name (no `./` prefix) and always qualify:

```mko
pull "strings"
pull "path"
pull "net/http"

fn main() {
    print(strings.trim("  hello  "))
    print(strings.concat(strings.split("a,b", ","), "-"))
    print_int(int(path.matches("*.mko", "main.mko")))
}
```

`join` and `match` are keywords, so the equivalents are `concat` and
`matches`. Full index: [STDLIB.md](../STDLIB.md) · book
[ch07-stdlib](ch07-stdlib.md).

### When to use packages instead

File imports work great within a single project. When you want to share code
across projects, or your codebase grows large enough to need separate build
units, reach for packages and workspaces -- covered in
[Chapter 10: Packages](ch10-packages.md).

## Editor setup

### VS Code

A Makori extension is available that provides:

- Syntax highlighting for `.mko` files
- Integration with `makori check` for inline diagnostics
- Format-on-save via `makori fmt`

Install it from the extensions marketplace or point your editor at the `.mko`
grammar file in the repository under `editors/vscode/`.

### Vim / Neovim

Add `.mko` filetype detection to your configuration:

```vim
autocmd BufRead,BufNewFile *.mko set filetype=mako
```

For LSP integration, configure the Makori language server in your LSP client
settings.

### General editor tips

- Set your editor to run `makori fmt` on save. This keeps all code in canonical
  style and avoids formatting debates.
- Map a keybinding to `makori check` for rapid feedback without a full build.
- The compiler emits standard error diagnostics with file, line, and column,
  so editors that can parse the `file:line:col: message` format will show
  inline errors.

## Common first-day commands

```bash
# Check your installation
mako version
mako doctor

# Create and run a project
mako init hello --name hello
cd hello
mako run main.mko

# Type-check without building
mako check main.mko

# Build a release binary
mako build --release main.mko

# Format your code
mako fmt main.mko

# Run tests
mako test examples/testing
```

## Troubleshooting

**"clang: command not found"** (Linux/Windows only)

On macOS, the native backend uses a bundled linker — clang is not needed.
On Linux: `sudo apt install gcc` (or `clang`). On Fedora: `sudo dnf install gcc`.
On Windows: install [LLVM](https://llvm.org/) or `choco install llvm`.

**"runtime headers not found"**

Either run `make install` again or set `MAKO_RUNTIME` to point at the runtime
directory.

**"permission denied" on `~/.local/bin/mako`**

Ensure `~/.local/bin` exists and is writable:

```bash
mkdir -p ~/.local/bin
make install
```

**Build cache issues**

If you suspect stale cache artifacts, clean and rebuild:

```bash
mako build --clean main.mko
```

## Next steps

You now have Mako installed, know how to create projects, and can build and run
programs. The next chapter is a comprehensive tour of the language syntax.

Next: [Language Tour](ch03-language-tour.md).
