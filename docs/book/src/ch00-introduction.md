# Introduction

Welcome to **The Mako Book** -- the official guide to learning and using the Mako
programming language.

## What is Mako?

Mako is a systems and backend language built for **speed first**, with
**first-class concurrency and parallelism**, plus clarity and safety.
**Syntax is Mako’s own**. Safety comes from ownership and arenas — **not a GC**.
Native performance is a design goal (compiled to C with `-O3 -flto`).

It compiles `.mko` source files to C, then links them via clang into a single
native binary. Memory safety uses `hold` / `share` and arenas. Concurrency and
parallelism are language features: structured `crew` / `kick` / `join`, `fan`
across cores, channels, actors — no free-fire leaks, no async coloring.

Mako is currently at version **0.4.0**. This book teaches idiomatic Mako as it
ships today. Identity checklist: [IDENTITY.md](../../IDENTITY.md).

| Area | Where |
|------|--------|
| Generics (structs/enums/bounds) | [language tour § Generics](ch03-language-tour.md#generics-019) · [GUIDE §6](../../GUIDE.md) |
| Channels (incl. struct/tuple + len/cap) | [ch. 6 concurrency](ch06-concurrency.md) · [howto/05](../../howto/05-concurrency.md) |
| Collections (maps/slices/bags) | [ch. 3](ch03-language-tour.md) · [cookbook](ch14-cookbook.md#collections-recipes) · [howto/10](../../howto/10-collections.md) |
| Low ceremony | [ERGONOMICS.md](../../ERGONOMICS.md) |

## Who is this book for?

This book is for programmers who want to build:

- Backend services (REST APIs, gRPC endpoints, WebSocket servers)
- Infrastructure tools (proxies, load balancers, protocol stacks)
- Command-line applications and developer tools
- Real-time systems with deterministic latency
- Database engines and storage layers
- AI services and data pipelines

You do not need prior systems programming experience, but you should be
comfortable with at least one programming language. The book starts from first
principles and builds up to advanced topics.

## A quick taste

Here is a small Mako program (Fibonacci):

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

Running it:

```bash
mako run hello.mko
# hello from mako
# 55
```

Methods use Mako’s `on` form; multi-return uses tuples:

```mko
struct Point {
    x: int
    y: int
}

on Point {
    fn distance(self) -> int {
        return self.x + self.y
    }
}

fn divmod(a: int, b: int) -> (int, int) {
    return (a / b, a % b)
}

fn main() {
    let p = Point { x: 3, y: 4 }
    print_int(p.distance())
    let q, r = divmod(17, 5)
    print_int(q)
    print_int(r)
}
```

Error handling with `Result` (compiler enforces handling):

```mko
fn parse_port(s: string) -> Result[int, string] {
    let v = parse_int(s)?
    if v <= 0 || v > 65535 {
        return error("port out of range")
    }
    Ok(v)
}

fn main() {
    match parse_port("8080") {
        Ok(p) => print_int(p),
        Err(e) => print(e),
    }
}
```

And a glimpse of ownership and concurrency:

```mko
fn main() {
    // hold gives move semantics -- use-after-move is a compile error
    hold let msg = "important data"
    process(msg)
    // print(msg)  // compile error: use of moved value `msg`

    // Arena allocators for request-scoped memory
    arena a {
        let mut buf = make([]int, 0, 1024)
        buf = append(buf, 42)
        print_int(buf[0])
    }
    // everything in arena `a` freed here -- one deallocation for the region
}

fn process(s: string) {
    print(s)
}
```

## How this book is organized

The book is split into chapters that build on each other. If you are new to Mako,
read chapters 1 through 6 in order. They cover installation, syntax, ownership,
and error handling -- the foundation you need for everything else.

| Chapter | What you learn |
|---------|----------------|
| [1. Preface](ch01-preface.md) | Why Mako exists, design philosophy |
| [2. Getting Started](ch02-getting-started.md) | Install, first project, tooling |
| [3. Language Tour](ch03-language-tour.md) | Syntax, types, operators, control flow |
| [4. Ownership](ch04-ownership.md) | `hold` / `share` / arenas / scope cleanup |
| [5. Errors](ch05-errors.md) | `Result`, `?` operator, error wrapping |
| [6. Concurrency](ch06-concurrency.md) | crew blocks, channels, actors |
| [7. Stdlib](ch07-stdlib.md) | Standard library packages by area |
| [8. Networking](ch08-networking.md) | HTTP, TLS, WebSocket |
| [9. Data](ch09-data.md) | JSON, SQL, file I/O |
| [10. Packages](ch10-packages.md) | `mako.toml`, dependencies, workspaces |
| [11. Speed & Safety](ch11-speed-safety.md) | Release builds, security model |
| [12. Cross-platform](ch12-cross-platform.md) | Build targets, WASI |
| [13. Tooling](ch13-tooling.md) | LSP, formatter, debugger |
| [14. Cookbook](ch14-cookbook.md) | Practical recipes (HTTP, collections, …) |
| [15. Appendix](ch15-appendix.md) | Keywords, types, map grid, flags |

## How to read this book

**If you are new to Mako:** Start at Chapter 2 and read sequentially through
Chapter 6. These chapters introduce the language foundations step by step, with
each concept building on the previous one. Do not skip the ownership chapter --
it is central to how Mako programs are structured.

**If you are building a service:** After the foundations, jump to Chapters 7
through 10 for standard library coverage, networking, data handling, and package
management.

**If you want recipes:** Chapter 14 is a cookbook index that links into the
`howto/` directory with focused, task-oriented guides.

**If something looks wrong:** The compiler is the source of truth. Run
`mako check` on your code and consult [GUIDE.md](../../GUIDE.md) for the
exhaustive syntax reference.

## Conventions used in this book

Code examples use the `.mko` extension and are formatted with `mako fmt`:

```mko
fn example() -> int {
    let x = 42
    return x
}
```

Terminal commands are shown with `$` or bare:

```bash
mako run main.mko
mako build --release main.mko
```

When a code example would produce a compile error, it is commented out with an
explanation:

```mko
hold let x = "data"
hold let y = x
// print(x)  // compile error: use of moved value `x`
print(y)
```

## Running the examples

All examples in this book are drawn from the `examples/` directory in the Mako
repository. You can run any of them:

```bash
mako run examples/hello.mko
mako run examples/result.mko
mako run examples/arena.mko
mako run examples/map.mko
```

The full test suite can be run with:

```bash
mako test examples/testing
```

## Getting help

- Run `mako --help` for command-line usage
- Run `mako doctor` to diagnose your installation
- Check [STATUS.md](../../STATUS.md) for what is implemented
- Check [GUIDE.md](../../GUIDE.md) for the current syntax reference

Let's get started. Turn to [Chapter 1: Preface](ch01-preface.md) to learn why
Mako exists and what problems it solves.
