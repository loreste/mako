# Mako language

Mako is a systems and backend language: clear to write, strict at compile time,
fast at runtime, and designed so **builds stay fast**.

**Guided tour:** [The Mako Book](book/).  
**Full syntax guide with verified `.mko` examples:** [GUIDE.md](GUIDE.md).  
**Reserved keywords (38, from lexer):** [KEYWORDS.md](KEYWORDS.md).  
This page is the short design overview. Product north star: [VISION.md](VISION.md).  
Honest matrix: [STATUS.md](STATUS.md). How-tos: [howto/](howto/).

## Design pillars

| Pillar | How |
|--------|-----|
| Clear | Concise keywords, braces, local inference, trailing-expression returns |
| Strict | Static types, no null, exhaustive `match`, `Result` / `Option` |
| Fast binaries | Native code via C (today) or a future object backend |
| Fast builds | Linear frontend; debug `-O0` by default; avoid whole-program analysis in the hot path |
| Concurrent | `crew` — structured scopes; jobs cannot outlive the crew |
| Parallel | `fan` — data-parallel map over cores |
| Memory | Ownership + `arena`; RC/manual later; **no mandatory GC** (optional GC is a later opt-in) |
| Safe by default | Bounds checks; unused `Result` is an error — see [SECURITY.md](SECURITY.md) |

Mako is its own language. Ideas can inspire, but syntax decisions should be
judged by whether they make Mako clearer, safer, and easier to teach. The goal
is a distinct Mako surface that backend developers recognize quickly and keep
using comfortably.

## Syntax Identity

Mako's surface should follow these rules:

- Prefer short, readable forms that fit backend code without ceremony.
- Make safety visible where it matters: `Result`, `Option`, `arena`, `hold`,
  `share`, `crew`, `actor`, and `unsafe` are part of the language identity.
- Avoid copying another language's full grammar just for familiarity.
- Keep punctuation purposeful; avoid sigil-heavy or annotation-heavy style.
- Let `mako fmt` define the canonical look so codebases feel consistent.

## Operators

`=` is assignment only. Comparisons use `==` `!=` `<` `>` `<=` `>=`.  
Logical: `&&` `||` `!` (and word forms `and`/`or`/`not`) with short-circuit.  
Bitwise: `&` `|` `^` `&^` `<<` `>>`, unary `^`. See [KEYWORDS.md](KEYWORDS.md) · [GUIDE.md](GUIDE.md) §2c.

## Imports

```mko
import "strings"
import "./lib.mko" as lib
import (
    "path"
    "fmt"
    x "./other.mko"
)
```

Brace form `import { "a"; "b" }` is also accepted. `mako fmt` groups 2+ imports into `import ( … )`.  
See [GUIDE.md](GUIDE.md) · [KEYWORDS.md](KEYWORDS.md).

## Target syntax feel

Everyday backend style — not academic:

```mko
fn handleCall(call: Call) -> Result {
    if call.valid() {
        return route(call)
    }
    return error("invalid call")
}
```

**v0.1 note:** programs today use `Result[int, string]`, `Ok` / `Err`, and
`error("...")` as sugar for `Err(...)`. Generics currently use `[T]`; the
vision prefers `List<T>`, `Map<K,V>`, `Result<T,E>` — see [VISION.md](VISION.md).

## Compile speed (fast compile target)

**Target:** for typical backend service size, compile times should be fast enough
for interactive edit-run loops.

**v0.1 reality**

- Frontend (lex/parse/typecheck/codegen) is intentionally simple and near-linear.
- Backend is clang. Debug builds use `-O0 -g`; release uses `-O3 -flto`.
- Use `mako build --time` to see frontend vs backend cost.
- Package cache + parallel units: [BUILD.md](BUILD.md) (Done).

**Design constraints (keep builds fast)**

- Prefer separate compilation over whole-program analysis in the hot path.
- Do not require heavy generics monomorphization by default.
- Typecheck stays separate from optimize; incremental compile is planned.

## Surface sketch

```mko
fn fib(n: int) -> int {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}

enum Shape {
    Circle(int),
    Rect(int, int),
    Point,
}

fn area(s: Shape) -> int {
    match s {
        Circle(r) => r * r,
        Rect(w, h) => w * h,
        Point => 0,
    }
}

fn fetch_both(a: string, b: string) -> Result[string, string] {
    crew t {
        let fa = t.kick(fetch(a))
        let fb = t.kick(fetch(b))
        return Ok(fa.join()? + fb.join()?)
    }
}

fn squares(xs: [int]) -> [int] {
    fan(xs, |x| x * x)
}
```

## Concurrency model

- **`crew name { ... }`** — Opens a scope. On exit, all kicked jobs are joined.
- **`name.kick(expr)`** — Schedules work; returns `Job[T]`.
- **`job.join()`** — Waits for the job; use `?` when the job yields `Result`.
- **`fan(collection, fn)`** — Parallel map across a thread pool.

No orphan background work: if it was kicked inside a crew, it finishes with that crew.

## Memory

Mako aims for **easy lifetimes** without a *mandatory* tracing GC and without
C-style `malloc`/`free` as the default.

| Tool | Role |
|------|------|
| Scope ownership | Values are released when their scope ends |
| `arena name { ... }` | Bump region for request/batch work; **one free** on exit |
| `hold T` | Unique/move with **CFG NLL** + labeled `break`/`continue` (`src/types/nll.rs`) |
| `share T` | Refcounted `share_int` / `share_clone` / `share_drop` + mid-scope NLL |
| Manual (roadmap) | Low-level systems escape hatch |
| Optional GC (later) | App-level opt-in only — never required for backends/systems |

```mko
arena a {
    let s = arena_text(a, "body")
    let xs = arena_ints(a, 64)
} // region freed here
```

Details: [VISION.md](VISION.md) · [SECURITY.md](SECURITY.md).

## Concurrency (today + target)

**Today:** `crew` / `kick` / `join` / `fan`, **channels**, **cancel**.  
**Next:** timeouts that are portable everywhere, async I/O — see [VISION.md](VISION.md).

- **`crew name { ... }`** — Opens a scope. On exit, all kicked jobs are joined.
- **`name.kick(expr)`** — Schedules work; returns `Job[T]`.
- **`job.join()`** — Waits for the job; use `?` when the job yields `Result`.
- **`job.join_timeout(ms)`** — Wait with a deadline (best-effort on macOS).
- **`crew.cancel()` / `crew.cancelled()`** — Stop starting new kicks; cooperative cancel flag.
- **`fan(collection, fn)`** — Parallel map across a thread pool.
- **`chan_new(cap)`** — Buffered `chan[int]`; `.send(x)`, `.recv()`, `.close()`.
- **Actors (seed)** — `actor_spawn` / `actor_send` / `actor_recv` / `actor_stop`
  (mailbox = channel; owned state in the loop). Target: `actor Session { receive … }`.
  Beachhead: session servers — see `examples/actor.mko` and [VISION.md](VISION.md).
- **TCP** — `tcp_listen` / `tcp_accept` / `tcp_close` / `tcp_write`.

```mko
let ch = chan_new(4)
crew t {
    let p = t.kick(producer(ch, 5))
    let c = t.kick(consumer(ch))
    print_int(c.join())
}
```

No orphan background work: if it was kicked inside a crew, it finishes with that crew
(or is skipped after `cancel`).

## Diagnostics

Errors print to stderr as `error:` with `file:line:col`, the source line, a
`^` caret, and often a `help:` hint. Try:

```bash
mako check path/to/bad.mko
```

## Testing

Mako testing keeps the low-friction package workflow developers expect:

| Convention | Mako |
|------------|------|
| Test file | `foo_test.mko` (same directory as code) |
| Test function | `fn TestAdd() { ... }` |
| Run tests | `mako test [path]` |
| Assertions | `fail("msg")`, `assert`, `assert_eq`, `assert_eq_str` |

```mko
// add.mko
fn add(a: int, b: int) -> int { return a + b }

// add_test.mko
fn TestAdd() {
    assert_eq(add(2, 3), 5)
}

fn TestAddTable() {
    let a = [1, 2]
    let b = [1, 3]
    let want = [2, 5]
    for i in 2 {
        assert_eq(add(a[i], b[i]), want[i])
    }
}
```

`mako test` discovers `*_test.mko`, merges sibling `.mko` package files, compiles a
harness that runs each `TestXxx`, and continues after a failed assert.
Exit code is non-zero if any test failed. Legacy `test_*.mko` with `main` still runs.

Filter: `mako test --run TestAdd` or `-r 'Test*'`.

Subtests (seed): call `t_run("case")` before asserts; failures print `TestXxx/case`.

## Errors and absences

There is no null. Use `Option[T]` and `Result[T, E]`. Propagate with `?`.
Handle with exhaustive `match`. **Discarding a `Result` is a compile error**
(unless you write `let _ = ...`).

`error("message")` builds a failure (`Err`) for short call sites.

## Match

`match` is exhaustive for `Result`, `Option`, `bool`, and user `enum`s.
A trailing expression in a function body is an implicit return.

## Stdlib (v0.1)

| Function | Purpose |
|----------|---------|
| `print` / `print_int` | stdout |
| `str_len` / `str_eq` / `str_contains` | strings |
| `int_to_string` | formatting |
| `len` | array or string length |
| `assert` | abort on false |
| `arena_text` / `arena_ints` / `arena_stamp` | allocate into an arena |
| `http_serve` | tiny HTTP/1.1 server (fixed body) |
| `http_echo` | HTTP/1.1 echo of method + path (JSON) |
| `chan_new` / `.send` / `.recv` / `.close` | int channels |
| `error` | construct `Err` from a string |

Web/TLS/DB vision: [STDLIB.md](STDLIB.md). Security: [SECURITY.md](SECURITY.md).
Tooling / optional GC / channels: [VISION.md](VISION.md).

## Compilation

```
.mko → lexer → parser → typecheck → C → clang → binary
         \________ frontend (keep linear) ________/
```

| Mode | Flag | clang |
|------|------|-------|
| Debug (default) | `mako build` / `mako run` | `-O0 -g` |
| Release | `mako build --release` | `-O2` |

## Roadmap (builds)

- Package-level caching and separate compilation
- Native object codegen (cut clang out of the hot path where possible)
- Parallel compile units
- Incremental typecheck
