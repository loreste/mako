# Mako

Mako is a compiled language for backend and systems work. You write `.mko`
files; Mako turns them into standalone native binaries — no garbage collector,
no VM, nothing extra to install next to them at runtime.

**Status: alpha (v0.4.20).** It works, it compiles real programs, people have
built things with it. It is not stable. APIs will change, features are missing,
and there are bugs. If that's fine with you, read on.

[mako-lang.com](https://mako-lang.com) · [Changelog](CHANGELOG.md) · [Roadmap](docs/ROADMAP.md) · [Status](docs/STATUS.md)

---

## Install

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

**Windows** — grab the `.zip` from [Releases](https://github.com/loreste/mako/releases),
or build from source with LLVM clang on PATH.

**From source** (needs Rust):

```bash
make install
mako version
```

You do not need Rust on the machine that runs Mako. The installer downloads a
prebuilt binary bundle.

---

## What it looks like

```mko
fn main() {
    let ch = make(chan[string], 4)
    crew t {
        let p = t.kick(produce(ch))
        for msg in range ch {
            print(msg)
        }
        let _ = p.join()
    }
}

fn produce(ch: chan[string]) -> int {
    let _ = ch.send("hello")
    let _ = ch.send("world")
    ch.close()
    return 0
}
```

```bash
mako init hello && cd hello
mako run main.mko
mako build --release main.mko -o hello
```

## What actually works

**Language.** Static types with local inference. `Result[T, E]` and `Option[T]`
with `?` propagation. Pattern matching. Enums with payloads. Generics
(monomorphized). Interfaces (structural, like Go). Closures. Tuples and
multi-return. Integer literals in decimal, hex (`0xFF`), binary (`0b1010`),
and octal (`0o77`) with `_` separators. `defer`. Labeled loops. F-strings.
Struct update syntax.

**Memory.** Ownership tracking with compile-time move checks. Arenas for
bulk allocation. Bounds checks in debug and release. Escape analysis.
Deterministic cleanup — no GC, no reference counting on the hot path.
The safety model is validated under ASan, UBSan, and TSan in CI but is
not proven complete. `unsafe` and FFI are outside the model.

**Concurrency.** `crew` / `kick` / `join` — structured concurrency where jobs
cannot outlive their scope. Typed channels (`chan[int]`, `chan[string]`,
`chan[T]`), `select`, `fan` for parallel map. Actors with mailboxes. No free
`go` keyword — every spawned task has an owner.

**Stdlib.** HTTP server and client. TLS (OpenSSL). WebSocket. JSON. SQLite and
Postgres. SIP parsing and building. HEP (Homer) ingest. UDP/TCP/Unix sockets.
File I/O. Regex. UUID. Base64. Binary buffers. Prometheus metrics. Crypto
(SHA-256, HMAC, PBKDF2, AEAD). Coverage is uneven — [STDLIB.md](docs/STDLIB.md)
records what has real tests and what only verifies its shape.

**Backends.** Two compilation paths: a C backend (mature, full language) and a
native backend (Cranelift for debug, LLVM for release). The native backend
passes 393/393 tests but does not cover every language feature yet. Both
produce standalone binaries.

**Packages.** `mako pkg` manages dependencies with a lockfile, SHA-256 content
hashes, and SemVer resolution. Supports path deps, git deps, local registry,
and remote HTTPS registry. Packages can be signed with ed25519 and verified
on fetch.

**Tooling.** `mako fmt`, `mako lint`, `mako test` (with JSON reports), `mako check`.
LSP server with completions, go-to-def, references, rename, diagnostics,
and inlay hints. VS Code extension.

## What does not work yet

- The native backend does not cover the full language (use `--backend c` for everything)
- No debugger product (lldb works with `#line` source mapping, but no IDE integration beyond seeds)
- Stdlib coverage is uneven — some APIs are shape-only
- No stable ABI promise
- Package registry is new and has no public hosted instance yet
- Windows HTTP engine is incomplete

[STATUS.md](docs/STATUS.md) has the full honest list.

## Concurrency

Mako has no free `go`. Every task belongs to a `crew`:

```mko
crew t {
    let a = t.kick(work(1))
    let b = t.kick(work(2))
    print(a.join())
    print(b.join())
}
// both tasks joined here, guaranteed
```

Channels are typed and work across kicked tasks:

```mko
let ch = make(chan[string], 8)
// send from one task, range-recv in another
for msg in range ch {
    print(msg)
}
```

## Errors

If a function returns `Result`, you have to handle it:

```mko
fn load(path: string) -> Result[string, string] {
    let data = read_file(path)?
    Ok(data)
}
```

## Testing

```bash
mako test examples/testing               # run all tests
mako test -r TestAdd -v                   # filter + verbose
mako test --sanitize address examples/testing  # under ASan
```

393 test files. The suite runs under ASan, UBSan, and TSan in CI.

## Docs

| | |
|---|---|
| [The Mako Book](docs/book/) | Start here |
| [Language Guide](docs/GUIDE.md) | Syntax reference |
| [Standard Library](docs/STDLIB.md) | What's included |
| [CLI Reference](docs/CLI.md) | Commands and flags |
| [Examples](docs/EXAMPLES.md) | Runnable programs |
| [Performance](docs/PERFORMANCE.md) | Benchmarks (including where Mako is slower) |
| [Soundness](docs/SOUNDNESS.md) | Memory safety program |
| [Security](docs/SECURITY.md) | Safety model |
| [Status](docs/STATUS.md) | What works, what doesn't |

## Editor support

VS Code extension with syntax highlighting, LSP, format-on-save, and a dark
theme. The language server (`mako lsp`) speaks stdio JSON-RPC.
See [editors/vscode/](editors/vscode/).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT
