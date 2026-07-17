# Mako language guide

**What works today** — syntax and APIs the compiler accepts, with examples under
`examples/`. Sources use the **`.mko`** extension (not `.mk`).

**Idiomatic style is Mako’s own.** Prefer `fn`, `let`, `struct`, `on Type { }`,
`hold` / `share` / `arena`, `crew` / `kick`. Dual Go-like spellings (`func`,
`:=`, bare `a int`) remain valid as **compat sugar**, not the brand.

| Doc | Role |
|-----|------|
| **This guide** | Verified syntax + how to use it |
| **[IDENTITY.md](IDENTITY.md)** | Our syntax identity + **% checklist** |
| [GO_SYNTAX_CHECKLIST.md](GO_SYNTAX_CHECKLIST.md) | Optional dual-form inventory (not preferred) |
| **[The Mako Book](book/)** | Guided tour (idiomatic Mako) |
| [COMPAT.md](COMPAT.md) | Dual forms / backward compatibility |
| [STATUS.md](STATUS.md) | Done matrix (adversarial) |
| [BUILD.md](BUILD.md) | Incremental cache, `-j`, residual clang |
| [PERFORMANCE.md](PERFORMANCE.md) | Release `-O3 -flto`, benchmarks |
| [DEBUG.md](DEBUG.md) | lldb/gdb, `dbg`, sanitizers |
| [SECURITY.md](SECURITY.md) | Memory safety + cache guarantees |
| [STDLIB.md](STDLIB.md) | HTTP library + std surface |
| [howto/](howto/) | Task-oriented how-to guides |
| [RELEASE.md](RELEASE.md) | Packaging / install |
| [KEYWORDS.md](KEYWORDS.md) | Full reserved-keyword list |
| [VISION.md](VISION.md) | North star |
| [ROADMAP.md](ROADMAP.md) | Sequencing |

**Legend:** unmarked = verified · **Target** = aspirational (VISION Later).  
**Mako identity strength:** [IDENTITY.md](IDENTITY.md) (**~86%**).  
STATUS north-star / MVP: **100%** (homebrew-core publish is the only external blocker).

---

## Mako-native syntax (preferred)

Canonical sample: [`examples/mako_style.mko`](../examples/mako_style.mko).

```mko
export struct Point {
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

    hold let n = 10
    arena a {
        let label = arena_text(a, "mako")
        print(label)
    }

    crew t {
        let job = t.kick(work())
        print_int(job.join())
    }
}
```

| Prefer (Mako) | Dual (compat) |
|---------------|---------------|
| `fn f(a: int) -> int` | `func f(a int) int` |
| `on T { fn M(self) … }` | `func (p T) M()` |
| `struct T { x: int }` | `type T struct { x int }` |
| `let` / `let mut` | `:=` / `var` |
| `export fn` | Capitalized names |
| `crew` / `kick` / `join` | — |
| `hold` / `share` / `arena` | — |

Goal: **simple everyday code**, **systems-grade control**, **unique Mako surface** —
and close the real [pain points of Go and Rust](PAIN_POINTS.md) without cloning either.

## Quickstart (install + init)

```bash
# From a checkout
make install                    # → ~/.local/bin/mako + ~/.local/share/mako/runtime
# or: ./scripts/install.sh

mako --version
mako init hello --name hello
cd hello
mako run main.mko               # uses installed runtime via binary-relative path
mako build main.mko             # binary named from mako.toml `name` when file is main.mko

# Backend API service scaffold
mako init mysvc --backend
cd mysvc
mako run main.mko

# Optional: multi-package workspace (local-only)
mako init myws --workspace
cd myws
mako check .
mako run -p app
```

`mako version` (also `mako --version` / `-V`) prints `mako version mako0.2.0 darwin/arm64`. Use `mako version -v` for an optional commit line.
Override headers if needed: `export MAKO_RUNTIME=/path/to/runtime`.

Incremental builds are **on by default** (`-j` / `MAKO_JOBS`, `--no-incremental` to disable) — see [BUILD.md](BUILD.md). Release: `mako build --release` → `-O3 -flto` ([PERFORMANCE.md](PERFORMANCE.md): optimized on microbenches).

For speed: pre-size `make([]T, 0, n)` / `make(map[K]V, n)`, use arenas for request scope, prefer `hold` over `share`, measure with `now_ns` + `./scripts/bench.sh`.

From a source tree without installing:

```bash
cargo run --release -- check examples/hello.mko
cargo run --release -- run examples/hello.mko
```

### Packages (`mako.toml`)

Local path deps are the useful surface today; remote git fetch is thin (needs `git`).

```bash
mako pkg init mylib                 # same scaffold as `mako init`
mako pkg list                       # name + deps; path/git status on disk
mako pkg fetch                      # clone git deps into `.mako/deps/` (needs git + network)
mako pkg add helper ../helper       # record / update path dep in [dependencies]
mako pkg add path=../helper         # same; name from basename
mako pkg remove helper              # drop a [dependencies] entry
mako pkg lock                       # pin content_hash in mako.lock
mako pkg audit                      # offline advisory + license policy check
```

Example `mako.toml` dependency lines:

```toml
[dependencies]
"helper" = { path = "../helper", version = "0.1.0" }   # version is informational only
"tool" = { git = "https://example.com/tool.git", tag = "v0.1.0", version = "0.1.0" }
```

`version` on path deps is checked with SemVer (`^` / `~` / exact) against the
path’s `mako.toml`. Registry-only deps (`"util" = { version = "^1.0.0" }`) resolve
from `.mako/registry/<name>/<ver>/` (highest match) — see `examples/pkg_registry/`.

`mako pkg list` exits non-zero if a path dep is missing **or** a git dep is not under `.mako/deps/` yet. `mako check` / `build` / `run` fail with the same **MISSING** wording (`run mako pkg fetch first` for git). Default CI does **not** live-fetch.

Local helpers:

```bash
mako pkg add helper ../helper
mako pkg add helper path=../helper
mako pkg add path=../helper          # name = directory basename
mako pkg add helper ../other         # idempotent: updates existing entry in place
mako pkg remove helper
mako pkg fetch                       # after declaring `{ git = "…", rev/tag/branch = "…" }`
```

### Path deps at compile time

When you `check` / `build` / `run` a `.mko` under a project with `mako.toml`, local `[dependencies]` **path** entries are merged into the program (transitive: A→B→C walks each dep’s own `mako.toml`):

| Dep layout | What is included |
|------------|------------------|
| Path to a `.mko` file | That file |
| Package dir with `lib.mko` | Only `lib.mko` (preferred) |
| Package dir without `lib.mko` | All top-level `.mko` except tests (`main` stripped) |

Symbols are namespaced by the dependency key declared by the **parent** package: `"helper" = { path = "…" }` → call `helper.add(...)` (same pattern as `import "…" as helper`). Nested packages keep the names their own manifests use (`helper` may call `core.scale`).

Demo: `examples/pkg_path_dep/` — `app` → `helper` → `core` (`mako run examples/pkg_path_dep/app/main.mko`).

### Workspace sketch (local-only)

Scaffold with `mako init DIR --workspace` (keeps default single-package `mako init` unchanged):

```bash
mako init myws --workspace
# → myws/mako.toml [workspace] members = ["lib", "app"]
# → myws/lib/lib.mko + myws/app/main.mko (path dep on lib)
```

Root `mako.toml` may also declare members by hand (no registry):

```toml
[workspace]
members = ["core", "helper", "app"]
```

From that root:

| Command | Behavior |
|---------|----------|
| `mako check .` | Typecheck each member’s `main.mko` or `lib.mko` |
| `mako build .` | Link each member that has `main.mko` (lib-only members skipped) |
| `mako test .` | Run `*_test.mko` under members that have tests |
| `mako fmt .` / `mako lint .` | Format / lint each member (optional `-p`) |
| `mako bench .` | Run `bench_*.mko` under members that have benches (optional `-p`) |
| `mako profile . -p app --json` | Build/run one member and report frontend/backend/run timings |
| `mako run .` | Run the unique member with `main.mko`, or error asking for `-p NAME` |
| `mako run -p app` | Run that member’s `main.mko` |
| `mako check/build/test/fmt/lint/bench -p NAME` | Focus a single workspace member |

Path deps between members still use `[dependencies]` path entries. See `examples/pkg_path_dep/`.

---

## Keywords

Hard-reserved in `src/lexer/mod.rs` (always keywords, never identifiers).
Full tables: **[KEYWORDS.md](KEYWORDS.md)**.

| Category | Keywords |
|----------|----------|
| Declarations | `fn` `struct` `enum` `actor` `receive` `interface` `extern` `const` |
| Bindings / ownership | `let` `mut` `hold` `share` `as` |
| Control flow | `if` `else` `while` `for` `in` `range` `break` `continue` `return` `defer` `match` |
| Literals / logic | `true` `false` `and` `or` `not` |
| Concurrency | `crew` `kick` `join` `fan` `select` `timeout` `default` |
| Memory | `arena` |

Not keywords: type names (`int`, `int8`, `int64`, `uint64`, `byte`, `float64`, `string`, …),
conversions `T(x)` / `bytes(s)`, `Ok`/`Err`/`assert*`/`t_run`, `len`/`cap`/`append`/`copy`.

---

## 1. Hello and program structure

A program is a `.mko` file with top-level `fn` / `struct` / `enum` / `actor` /
`interface` / `const` / `extern "C"`. Entry point is `fn main()`.

```mko
// examples/hello.mko
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

---

## 2. Types and strong typing

### Built-in types

| Type | Notes |
|------|--------|
| `int` | Platform natural; C backend → `int64_t`, distinct in checker |
| `int64` / `int32` / `int8` | Fixed widths; `int8(x)` range-checks `-128..127` at runtime |
| `uint64` | Unsigned 64-bit; reject negative on convert from signed |
| `byte` | Unsigned 8-bit; element of `[]byte` |
| `float`, `bool`, `string` | No silent mix with ints |
| `[]int` / `[]int64` / `[]byte` | Slices (see § Slices / Bytes) |
| `Option[T]`, `Result[T, E]` | No nil |
| `chan[T]` | Typed channels (`chan[int]` common) |
| Named structs / enums | User-defined |

### Integer family

```mko
// examples/integers.mko · examples/bytes.mko
let a: int = 10
let b: int64 = 20
let c: int32 = 7
let d: int8 = int8(42)
let u: uint64 = uint64(7)
print_int64(int64(a) + b)
print_int8(d + int8(1))
// a + b  // error — cannot mix integer kinds
```

Rules:

- Untyped literals default to `int`; may fill `int`/`int64`/`int32`/`int8`/`byte` annotations (not `uint64` without `uint64(n)`).
- Named kinds **do not** mix — convert with `int64`/`int32`/`int8`/`uint64`/`byte`/`int`.
- Backend: signed kinds → `int64_t`; `[]byte` → `MakoByteArray` (`uint8_t*`).
- `int8(x)` aborts if out of `-128..127`; `uint64` from negative signed aborts.

Bad: `examples/bad/int_mix_int64.mko`, `int32_mix_int64`, `int8_mix_int64`.

### Explicit conversions `T(x)`

```mko
// examples/convert.mko
let a: int = 10
let b = int64(a)
let f = float64(a)
print(string(a))           // "10"
let buf = bytes("hi")      // string → []byte
print(string(buf))
```

| From \\ To | int family | `int8` | `uint64` | `byte` | `float64` | `string` | `[]byte` |
|------------|------------|--------|----------|--------|-----------|----------|----------|
| int family | `int(x)`… | `int8(x)` ✓ | `uint64(x)` ✓≥0 | `byte(x)` | `float64(x)` | `string(x)` | — |
| `float64` | truncates | via int8 | via uint64 | via byte | identity | — | — |
| `string` | — | — | — | — | — | identity | `bytes(s)` |
| `[]byte` | — | — | — | index | — | `string(b)` | identity |

`float` ≡ `float64` today. Prefer **`[]byte(s)`** or `bytes(s)`.

Compile-time range: constant `int8(n)` / `byte(n)` / `uint64(n)` checked at
`mako check`; non-constants still runtime-abort.

### Rules enforced today

- No `int + string` (or other mixed arithmetic); no silent `int`/`int64` mix
- Call arity and argument types must match
- Return type must match (`return` or trailing expression)
- Annotated `let x: T = …` and `mut` assign are strict (literals may inhabit int/int64)
- `Option` / `Result` cannot be used as bare numbers — unwrap with `match` / `?`
- `chan.send` value must match element type
- Exhaustive `match` on enums / Option / Result
- Interfaces require `Iface_method` implementations

Good: `examples/types_ok.mko`, `examples/integers.mko`. Intentional failures: `examples/bad/*.mko`
(`mako check` must error).

```mko
fn add(a: int, b: int) -> int {
    return a + b
}

fn main() {
    let n: int = 1
    print_int(add(n, 2))
}
```

## 2c. Operators

Assignment is `=` only. Equality is `==` (never single `=`).

| Class | Operators |
|-------|-----------|
| Compare | `==` `!=` `<` `>` `<=` `>=` |
| Logical | `&&` `\|\|` `!` (also keywords `and` / `or` / `not`) |
| Arithmetic | `+` `-` `*` `/` `%` |
| Bitwise | `&` `\|` `^` `&^` `<<` `>>` · unary `^x` (complement) |

- `&&` / `||` **short-circuit** (right side not evaluated when unnecessary).
- `!!x` is two unary `!`; there is no special `!!` token.
- Leading `|params| { … }` is still a lambda; infix `|` is bitwise or.
- Tests: `examples/testing/operators_go_test.mko`.

```mko
if x == 0 || y > 1 {
    return
}
let bits = (flags &^ mask) << 2
assert(!!ok)
```

---

## 2b. Slices

Literals `[1, 2, 3]` are **slices** (header: ptr, len, cap). Default element type is
`int`; annotate for `[]int64` / `[]int32` / `[]byte`:

```mko
let mut s = [1, 2, 3]              // []int
let mut a: []int64 = [10, 20, 30]  // []int64
let mut b: []byte = [72, 105]      // []byte
```

Type syntax: `[]T` (preferred) or `[T]`.

```mko
// examples/slice.mko · examples/slice64.mko · examples/bytes.mko
let mut s = [1, 2, 3]
print_int(len(s))
print_int(cap(s))

let t = s[1:3]      // shares backing store
t[0] = 99           // visible on s[1]
s = append(s, 4)    // may grow; assign result back

let n = copy(dst, s)   // returns count copied

// string <-> []byte (copying bridge)
let c = bytes("mako")     // helper
let d = []byte("mako")    // sugar (same as bytes())
print(string(c))
print(string(d[1:3]))

// pre-sized make
let s = make([]int, 3, 8) // len 3, cap 8
let z = make([]byte, 2)
let names = make([]string, 0, 4)
names = append(names, "mako")
let fs = make([]float, 2)          // examples/float_slice.mko
// also: let xs: []string = ["a", "b"]  · examples/str_slice.mko
let grid: [][]int = [[1, 2], [3]]  // nested
let rows = make([][]int, 0, 4)
rows = append(rows, [10, 20])
```

| Syntax | Meaning |
|--------|---------|
| `[…]` / `let b: []byte = […]` | Slice literal (`[]int` / `[]byte`) |
| `let xs: []string = ["a", "b"]` or `["a","b"]` | String slice |
| `let xs: []float = [1.0, 2.0]` / `make([]float, n[, cap])` | Float slice |
| `let g: [][]T = [[…], …]` / `make([][]T, …)` | Nested slices |
| `[]byte(s)` or `bytes(s)` / `string(b)` | String ↔ bytes conversion |
| `make([]int\|[]byte\|[]string\|[]float\|[]bool\|[][]T, …)` | Pre-sized allocation |
| `[]bool` / `[]Enum` | Bool and enum element slices |
| `s[i:j]`, `len`/`cap`/`append`/`copy` | Slice operations |

Compile-time: `int8(200)` / `byte(300)` rejected at `mako check` when the arg is a constant
(`examples/bad/int8_literal_oor.mko`, `byte_literal_oor.mko`). Runtime still aborts for non-const OOR.

`append` / `copy` are type-safe. Tests: `slice_test`, `slice64_test`, `bytes_test`,
`make_bytes_test`, `str_slice_test`, `float_slice_test`, `nested_slice_test`,
`map_bool_test` (`[]bool`), `map_enum_test` (`[]Enum`).

---

## 3. Functions and trailing returns

```mko
fn greet(name: string) -> string {
    return "hi " + name
}

fn area(w: int, h: int) -> int {
    // trailing expression = return (when no explicit return in body)
    match Ok(w * h) {
        Ok(v) => v,
        Err(e) => 0,
    }
}
```

`void` / omitted return: procedure. Missing return on a non-void function is a
type error (`examples/bad/missing_return.mko`).

---

## 4. Control flow — `while`, `for`, `range`, `break`, `continue`, `defer`

```mko
// examples/while.mko · examples/range.mko · examples/break_continue.mko · examples/defer.mko
fn main() {
    let mut i = 0
    while i < 10 {
        i = i + 1
        if i < 3 { continue }
        if i > 5 { break }
        print_int(i)
    }

    let s = [10, 20, 30]
    for i, v in range s {  // index + value
        print_int(i)
        print_int(v)
    }
    for i in range s { }   // indices only
    for _, v in range s { } // values only (`_` is blank)
    for range s { }        // no binders

    for i in range 3 { }   // 0..2 (integer range)
    for j in 3 { }         // legacy: same as range n
    for v in s { }         // legacy: values only

    defer print("cleanup") // LIFO on exit / return; or `defer { … }`
    // NLL: defer bodies are checked at function exit — cannot use a moved `hold`
}
```

`range` over `[]int` / `[]int64` / `[]byte` / `string`.  
On **strings**, `for i, r in range s` yields **runes** (Unicode code points as `int`); `i` is the **byte** offset.  
`len(s)` / `s[i]` / `s[i:j]` stay **byte**-based. Use `rune_count(s)` for rune length.  
`break` / `continue` only inside `for` / `while` (`examples/bad/break_outside.mko`).
**Labeled loops:** `outer: while … { break outer }` / `continue outer`
(`examples/labeled_break.mko`; unknown label → `examples/bad/labeled_break_unknown.mko`).
Maps and channels: see §4c / §4e.

---

## 4b. Strings

UTF-8 text: **byte** view for `len` / index / slice; **rune** view for `range` / `rune_count`.

```mko
// examples/strings.mko
fn greet(name: string) -> string {
    return "hello, " + name
}

fn main() {
    let s = "hi\tthere\n"     // escapes: \n \t \r \\ \"
    print_int(len(s))         // byte length (== str_len(s))
    print_int(rune_count(s))  // Unicode code points
    print("ma" + "ko")        // concat → string
    if s == "x" { }           // == / != only (no < > on strings)

    let hello = "hello"
    print_int(int(hello[0]))  // s[i] → byte; convert to print
    print(hello[1:4])         // "ell" — byte substring

    let b = []byte("Hi!")     // or bytes(s)
    print(string(b))
    print(string(42))         // int → decimal string

    if str_eq("a", "a") { }
    if str_contains("hello", "ell") { }

    // range yields runes; index is byte offset
    for i, r in range "café" {
        print_int(i)
        print_int(r)          // é → 233
    }
}
```

| Op | Result | Notes |
|----|--------|-------|
| `"…"` | `string` | `\n` `\t` `\r` `\\` `\"` |
| `s + t` | `string` | concat |
| `s == t` / `!=` | `bool` | no ordering |
| `len(s)` / `str_len(s)` | `int` | **byte** length |
| `rune_count(s)` | `int` | Unicode code points |
| `s[i]` | `byte` | bounds-checked; OOB aborts |
| `s[i:j]` / `s[:j]` / `s[i:]` / `s[:]` | `string` | byte slice (copy) |
| `for i, r in range s` | — | **runes**; `i` = byte offset |
| `[]byte(s)` / `bytes(s)` | `[]byte` | copy |
| `string(b)` / `string(n)` | `string` | from bytes / int |
| `str_eq` / `str_contains` | `bool` | helpers |

Multibyte: `len("café")` is **5**, `rune_count("café")` is **4**. Index/slice are byte offsets.  
OOB: `examples/bad/string_index_oob.mko`. Tests: `strings_test`.

**Builder** (growable buffer):

```mko
// examples/builder.mko
let mut b = str_builder()
builder_write(b, "ma")
builder_write(b, "ko")
builder_write_byte(b, byte(33))
print(builder_string(b))   // "mako!"
print_int(builder_len(b))
```

---

## 4c. Maps

Hash maps (open addressing). **Keys:** `int`, `string`, `float`, **`bool`**,
**structs**, or **enums**. **Values:** same set **or slices** `[]T` — any combo,
including `map[Point]Label`, `map[Color]int`, `map[string][]int`,
`map[string][][]int`, `map[Point][]int` / `map[Color][]string`, nested
`map[string]map[string]int` (depth 2), set-style `map[string]bool`, and
`map[bool]int`. Pack types work as keys or values.

Float keys: `+0.0` / `-0.0` are the same key; all NaNs share one key.
Struct keys: field-wise equality + stable field hash (strings by content).

```mko
// map.mko · map_struct_test.mko · map_float_test.mko · map_struct_key_test.mko
let mut m = make(map[string]int)
m["a"] = 1
print_int(m["a"])          // 1
print_int(m["missing"])    // 0 — zero value (no panic)
if has(m, "a") { }         // presence
let v, ok = m["a"]         // comma-ok pattern
if ok {
    print_int(v)
}
delete(m, "a")
print_int(len(m))

for k, v in range m {      // order unspecified
    print(k)
    print_int(v)
}

let mut mi = make(map[int]int)
mi[10] = 100

let mut ms = make(map[string]string)
ms["x"] = "hello"

let mut mf = make(map[int]float)
mf[1] = 2.5

let mut fi = make(map[float]int)
fi[1.5] = 3

struct Point { x: int, y: int }
struct Label { text: string, id: int }
let mut pts = make(map[int]Point)
pts[1] = Point { x: 1, y: 2 }
let mut pf = make(map[float]Point)
pf[1.5] = Point { x: 3, y: 4 }
let mut by_pt = make(map[Point]int)
by_pt[Point { x: 1, y: 2 }] = 10
let mut by_ss = make(map[Point]Label)
by_ss[Point { x: 0, y: 0 }] = Label { text: "origin", id: 0 }
let mut seen = make(map[string]bool)
seen["a"] = true
let mut by_b = make(map[bool]int)
by_b[true] = 1
enum Color { Red, Green }
let mut by_e = make(map[Color]int)
by_e[Red] = 1
let mut statuses = make(map[int]Color)
statuses[1] = Green
let mut groups = make(map[string][]int)
groups["a"] = [1, 2, 3]
let mut by_pt_rows = make(map[Point][]int)
by_pt_rows[Point { x: 1, y: 2 }] = [10, 20]
let mut by_e_rows = make(map[Color][]string)
by_e_rows[Red] = ["hot"]
let mut nested = make(map[string]map[string]int)
let mut row = make(map[string]int)
row["x"] = 1
nested["a"] = row
print_int(nested["a"]["x"]) // 1

// Helpers (all map kinds):
let ks = maps_keys(m)
let vs = maps_values(m)
let c = maps_clone(m)
assert_eq(maps_equal(m, c), 1)
maps_copy(c, m)
maps_clear(c)
```

| Op | Notes |
|----|-------|
| `make(map[K]V)` / `make(map[K]V, hint)` | allocate |
| `m[k] = v` / `m[k]` | insert / get (missing → zero) |
| `has(m, k)` | `bool` presence |
| `let v, ok = m[k]` | comma-ok: value + present |
| `delete(m, k)` | remove |
| `len(m)` | entry count |
| `for k, v in range m` | iteration; order unspecified |
| `maps_keys` / `maps_values` | `[]K` / `[]V` |
| `maps_clone` / `maps_equal` / `maps_copy` / `maps_clear` | bulk helpers (shallow for nested maps / channel maps) |

Missing key → zero value (`0` / `""` / `false` / empty slice / **nil** inner
map with `len` 0 / **None** / **Err("")** for bag values / **nil channel** for
`map[K]chan[T]`). Nested maps support depth **2–3** (`map[K]map[K2]V` and
`map[K]map[K2]map[K3]V`); depth 4+ is rejected. `maps_clone` / `maps_equal`
compare/copy outer entries by **inner-map pointer** identity (channel maps
likewise use channel-pointer identity). Bag-value maps (`map[K]Option[T]`,
`map[K]Result[T,E]`) store bags by value; match on `m[k]` works for Some/Ok
arms. Channel-value maps store channel handles.

Wrong key/value combo rejected at check (`examples/bad/map_key_type.mko`).
Struct keys may hold slice/map/Option/Result/enum fields — eq/hash use
identity or structural bag helpers (not invalid C aggregate `==`).

**Compile cost:** map/bag/slice monomorph helpers are **demand-driven** — only
shapes that appear in the compilation unit are emitted (not a full N² grid
over every named type). Large libraries stay roughly O(used maps). See
[howto/10-collections.md § Compile cost](howto/10-collections.md#compile-cost-demand-driven-monomorphs).

Tests: `map_test`, `map_struct_test`, `map_float_test`, `map_struct_key_test`,
`map_bool_test`, `map_enum_test`, `map_slice_test` (`map[K][]T` incl. named keys),
`map_nested_test` (`map[K]map[K2]V`), `map_depth3_test` (`map[K]map[K2]map[K3]V`),
`map_map_slice_test` (`map[K]map[…][]T`),
`map_nested_slice_test` (`map[K][][]T`), `slice_map_test` (`[]map` / `map[K][]map`),
`map_option_result_test` (`map[K]Option[T]` / `map[K]Result[T,E]`),
`map_option_slice_test` (`map[K][]Option[T]` / `map[K][]Result[T,E]`),
`map_option_of_slice_test` (`map[K]Option[[]T]` / `map[K]Result[[]T,E]`),
`map_tuple_test` / `map_tuple_struct_test` (`map[K](T,U)` incl. Struct/Enum),
`map_option_of_map_test` (`map[K]Option[map]` / `Result[map]`),
`map_chan_test` (`map[K]chan[T]`), `map_slice_chan_test` (`map[K][]chan[T]`),
`map_option_chan_test` (`Option[chan]` / `map[K]Option[chan]` / `Result[chan]`),
`map_option_chan_nested_test` (`[]Option[chan]` / `Option[[]chan]` maps),
`map_chan_nested_slice_tuple_test` (`[][]chan` / `(chan, scalar)` maps),
`map_tuple_chan3_test` (3-tuples with channel field),
`map_nested_option_chan_test` (`Option[Option[…]]` / struct-chan 3-tuples),
`map_option_result_nested_test` (`Option[Result]` / triple Option / `Result[Result]` maps),
`map_nested_bag_slice_test` (nested bag slices / optional bag slices),
`map_tuple_bag_test` (Option/Result fields in map tuples),
`nested_slice_test`, `struct_slice_fields_test`, `lang_residuals_test`.
`Option[map[K]V]` / `Result[map[K]V, E]` work with `None` / `Some` / `Ok` and match
unboxing for SI/II/SS, float/bool key maps, and monomorphized map pointers.
Low-ceremony patterns: [ERGONOMICS.md](ERGONOMICS.md).  
Hands-on: [howto/10-collections.md](howto/10-collections.md) · book tour
[ch03](book/src/ch03-language-tour.md) · [cookbook](book/src/ch14-cookbook.md#collections-recipes).

---

## 4c-2. Concurrent Maps (CMap)

`CMap` is a built-in concurrent hashmap designed for high-throughput key-value
workloads. It uses lock-free reads and per-stripe spinlock writes (512 stripes,
FNV-1a hash, 1M initial capacity). Safe to share across `crew` tasks without
`hold`/`share` concerns.

```mko
// examples/cmap.mko
fn main() {
    let m = cmap_new()
    cmap_set(m, "hello", "world")
    print(cmap_get(m, "hello"))       // "world"
    print_int(cmap_has(m, "hello"))   // 1
    print_int(cmap_len(m))            // 1

    let new_val = cmap_incr(m, "counter", 5)   // atomic increment
    print_int(new_val)                          // 5

    print_int(cmap_del(m, "hello"))   // 1 (existed)
    print_int(cmap_len(m))            // 1 (only "counter" remains)
}
```

| Op | Notes |
|----|-------|
| `cmap_new()` | Create a new concurrent map |
| `cmap_set(m, key, value)` | Set a key-value pair |
| `cmap_get(m, key)` | Get value (returns `""` if missing) |
| `cmap_has(m, key)` | Check existence (1/0) |
| `cmap_del(m, key)` | Delete key (returns 1 if existed) |
| `cmap_len(m)` | Entry count |
| `cmap_incr(m, key, delta)` | Atomic increment, returns new value |

Thread-safe by design: multiple `crew` tasks can read and write the same `CMap`
concurrently without channels or mutexes. Runtime: `runtime/mako_cmap.h`.

---

## 4d. UUID

RFC 4122 **UUID v4** (CSPRNG: `arc4random_buf` on Apple, `/dev/urandom` elsewhere).

```mko
// examples/uuid.mko
let id = uuid_v4()
let s = uuid_string(id)       // "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx"
let id2 = uuid_parse(s)       // nil Uuid if invalid
if uuid_eq(id, id2) { }
if uuid_parse_ok(s) { }
if uuid_is_nil(uuid_nil()) { }
let r = uuid_check(s)         // Result[int, string]
match r {
    Ok(_) => print("valid")
    Err(e) => print(e)
}
```

| API | Result |
|-----|--------|
| `uuid_v4()` | `Uuid` |
| `uuid_string(u)` | canonical lowercase hex + hyphens (len 36) |
| `uuid_parse(s)` | `Uuid` (nil on failure) |
| `uuid_parse_ok(s)` | `bool` |
| `uuid_eq` / `uuid_is_nil` / `uuid_nil` | compare / nil |
| `uuid_check(s)` | `Result[int, string]` |

Tests: `uuid_test`. Invalid: `examples/bad/uuid_bad.mko`.

---

## 4e. Range over channel

```mko
// examples/chan_range.mko
let ch = chan_new(4)
// … send then ch.close()
for v in range ch {   // recv until close
    print_int(v)
}
```

Requires `range`. Values only (`for v in range ch`). Ends when the channel is closed and drained.

---

## 5. Result, Option, `?`, `error()`, match

No null. Failures are values.

```mko
// examples/result.mko
fn parse_positive(n: int) -> Result[int, string] {
    if n <= 0 {
        return error("must be positive")
    }
    return Ok(n)
}

fn double_positive(n: int) -> Result[int, string] {
    let v = parse_positive(n)?
    return Ok(v * 2)
}

fn show(r: Result[int, string]) -> int {
    match r {
        Ok(v) => v,
        Err(e) => -1,
    }
}
```

```mko
// examples/match.mko — Option + enums
fn option_or(o: Option[int], fallback: int) -> int {
    match o {
        Some(v) => v,
        None => fallback,
    }
}
```

`?` early-returns the empty case and unwraps the payload:

| In a function returning… | `expr?` when `expr` is… | On empty | On success |
|--------------------------|-------------------------|----------|------------|
| `Result[T, E]` | `Result[T, E]` | `return` that `Err` | `T` (int / bool / string / float / **struct** / **[]T** / **map** / nested Option·Result) |
| `Option[T]` | `Option[T]` | `return None` | `T` (same payload set) |

Mismatched carriers are rejected (`Option` `?` inside a `Result` function, and the reverse).
Chained `?` works (`let a = f()?; let b = g()?`).
Tests: `wave37`–`wave39_queue_test.mko`.

```mko
// examples/wrap_err.mko · examples/errors_wrap.mko — wrap / is
let r = wrap_err(error("boom"), "parse")  // Err("parse: boom")
let ok = wrap_err(Ok(7), "unused")        // still Ok(7)
let e = errorf("missing %s", "config.toml")
assert(error_is(e, "config.toml"))
let msg = error_string(wrap_err(e, "load"))  // "load: missing config.toml"
```

Prefer `Result` + `?` + `wrap_err` over abort for expected failures. Runtime
aborts print a short message + debugger hint ([DEBUG.md](DEBUG.md)).

Unused `Result` as a statement is a compile error — use `?`, `match`, or
`let _ = …`.

**Debug:** `dbg(n)` / `dbg_str(s)` print `[dbg] file:line: …` on stderr (default
builds use clang `-g`). See [DEBUG.md](DEBUG.md).

---

## 6. Structs, enums, interfaces, generics

```mko
// examples/struct_slice.mko · nested_struct.mko · arena_struct.mko
struct Point {
    x: int
    y: int
}

let mut p = Point { x: 3, y: 4 }
print_int(p.x)
p.x = 10

let mut xs = make([]Point, 0, 4)
xs = append(xs, Point { x: 1, y: 2 })
let lit: []Point = [Point { x: 7, y: 8 }]

// Nested fields + assign
struct Addr { city: string zip: int }
struct Person { name: string addr: Addr }
let mut person = Person { name: "Ada", addr: Addr { city: "Paris", zip: 75001 } }
person.addr.city = "Lyon"

// Inside `arena a { … }`, `make([]Point, …)` and `append` grow from the arena
// (see examples/arena_struct.mko · arena_append.mko)
```

```mko
// examples/match.mko
enum Shape {
    Circle(int),
    Rect(int, int),
    Point,
}

fn describe(s: Shape) -> int {
    match s {
        Circle(r) => r * r,
        Rect(w, h) => w * h,
        Point => 0,
    }
}
```

```mko
// examples/generics_iface.mko · examples/iface_method.mko · examples/iface_self.mko
// examples/iface_dyn.mko · iface_multi.mko · iface_unit_dyn.mko
interface Writer {
    fn write(string) -> int
}

fn Writer_write(s: string) -> int {
    print(s)
    return str_len(s)
}

// Method sugar: `recv.write(s)` → `Writer_write(s)`
// With self: `fn Adder_add(self: Counter, delta: int)` → `c.add(5)` passes `c` first.
// Dyn: `let a: Adder = c` boxes; multi-concrete via `Adder_Counter_add` / `Adder_Doubler_add`.
// No-self dyn: `let w: Writer = 0` uses a unit vtable.
```

```mko
// examples/enum_method.mko — associated methods on enums
fn Shape_area(self: Shape) -> int { … }
print_int(Circle(5).area())
```

`List[int]` / `Result[int, string]` work; angle-bracket forms are accepted in
places. Struct values, field assign, nested fields, and `[]Struct` (including
arena `make` + arena `append`) are supported. Interface methods are free
functions `Iface_method` or `Iface_Concrete_method` with optional leading `self`;
fat-pointer interface values box concrete receivers (or unit for no-self).

### User generics (0.2.0) — monomorphized

```mko
// Function generics (also dual: fn id<T>(x: T) -> T)
fn identity[T](x: T) -> T {
    return x
}

// Interface bounds — structural method set
interface Describable {
    fn describe(self) -> string
}

fn get_description[T: Describable](thing: T) -> string {
    return thing.describe()
}

// Generic structs / enums — type args required at construction
struct Pair[T] {
    a: T
    b: T
}

enum MyBox[T] {
    Val(T)
    Nothing
}

fn make_pair[T](a: T, b: T) -> Pair[T] {
    return Pair[T] { a: a, b: b }
}

fn wrap_int(v: int) -> MyBox[int] {
    return Val(v)
}

fn main() {
    let p = Pair[int] { a: 1, b: 2 }
    let s = make_pair("hi", "lo")
    match wrap_int(9) {
        Val(v) => print(v),
        Nothing => {},
    }
}
```

| Form | Notes |
|------|--------|
| `struct Foo[T] { … }` | One C monomorph per concrete `Foo[int]`, `Foo[string]`, … |
| `enum Box[T] { Val(T), … }` | Match on monomorph variants |
| `fn f[T: I](…)` | Call sites must satisfy interface `I` |
| Nested | `Box[Pair[int]]` supported |

Tests: `examples/testing/generic_struct_test.mko`, `generic_enum_test.mko`,
`generic_bounds_test.mko`, `generic_adversarial_test.mko`,
`examples/bad/generic_bound_fail.mko` (must fail).

### Iterator protocol (seed)

Types with `fn next(self) -> Option[T]` (as `Type_next`) can drive `for … in`.
**By-value `self` does not mutate the outer value** — a `next` that always
returns `Some(self.current)` will not terminate. Prefer explicit mutation
patterns until mut-self iterators land. See `examples/testing/iterator_test.mko`.

### Mutable captures (seed)

Lambdas that **assign** to outer locals route those captures through a heap
cell. Everyday by-value capture still works (`|x| x + n`). Multi-statement mut
lambdas remain residual polish (`examples/testing/mutable_closure_test.mko`).

---

## 7. Ownership: let, mut, arena, hold, share

```mko
let x = 1
let mut y = 2
y = y + 1
```

```mko
// examples/arena.mko · examples/arena_slice.mko
arena a {
    // request-scoped work
    let mut s = make([]int, 3, 8)  // backing store from arena
    s[0] = 10
}
```

Inside `arena name { … }`, `make([]int, …)` / `make([]byte, …)` allocate from that arena
(freed when the arena exits). Outside arenas, `make` uses the heap as before.
```mko
// examples/hold_move.mko — hold moves on rebind
hold let x = 7
hold let y = x
print_int(y)
// print_int(x)  // error: use of moved value `x`
```

```mko
// examples/hold_into_call.mko — hold moves into call args
fn id(n: int) -> int { return n }
hold let x = 42
print_int(id(x))
// print_int(x)  // error after move into id
```

```mko
// examples/hold_reassign.mko — assign into live hold before move
hold let mut x = 7
x = 9
print_int(x)
```

Intentional failures (must `mako check` error):

- `examples/bad/hold_use_after_move.mko`
- `examples/bad/hold_double_move.mko`
- `examples/bad/hold_move_into_call.mko`
- `examples/bad/hold_index_after_move.mko`
- `examples/bad/hold_assign_after_move.mko`
- `examples/bad/hold_field_after_move.mko`
- `examples/bad/hold_partial_reuse.mko` — cannot re-read a moved non-Copy field
- `examples/bad/hold_partial_whole.mko` — cannot use whole value after partial move
- `examples/hold_copy_reread.mko` — Copy fields (`int`, …) may be re-read
- `examples/hold_copy_int.mko` — Copy `hold` bindings may be re-read / rebound without move
- `examples/bad/hold_nested_partial_reuse.mko` — nested path reuse
- `examples/bad/share_mut.mko` — `share let mut` rejected
- `examples/bad/share_assign.mko` — cannot assign to `share let`
- `examples/bad/share_mut_conflict.mko` — cannot mutate source while `share_int` live
- `examples/bad/share_mut_arg.mko` — cannot pass shared local into `mut` param
- `examples/bad/share_double.mko` — cannot `share_int` the same local twice while live

```mko
// examples/hold_partial.mko · hold_nested_partial.mko
hold let p = Point { x: 1, y: 2 }
let x = p.x   // moves only x
print_int(p.y) // y still usable

hold let o = Outer { inner: Inner { a: 1, b: 2 }, n: 9 }
let a = o.inner.a  // moves path "inner.a" only
print_int(o.inner.b)
```

```mko
// examples/ownership.mko — share RC seed
hold let a = 1
share let s = share_int(a)
print_int(share_get(s))
share_drop(s)
```

**Hold reads:** full reads of a non-Copy `hold` binding still consume it. Field access moves
only that field path (`hold_partial`, nested `o.inner.a`). Shared-borrow seed:
`share let` is immutable; `share_int(x)` blocks mutation of `x` until
`share_drop` or **block end** (`}` auto-drops — `share_nll`), or **mid-scope** after
the last use of the share binding in a straight-line block (`share_mid_scope`;
bad `share_mid_scope_live` when still used later). If/else: share ended on **both**
arms → mut after join OK (`share_if_else_end`); still live on both → conflict
(`examples/bad/share_if_else_live.mko`). **Diverging arms** (`return` / `break` /
`continue`): moves and share ends in a diverging arm do not poison code after the
`if` (`hold_if_return_ok`; bad `share_if_return_live` when the skip-then path still
borrows). Without `else`, a share that may survive on the skip-then path stays live
(`examples/bad/share_if_noelse_live.mko`). While: shares introduced in the body end
each iteration — re-borrow next iter OK (`share_loop_iter`; bad `share_loop_live`
if outer share still used while mutating). `for range` restores share state the same
way (`share_for_iter`; bad `share_for_live`). Nested if inside while: share ended on
both arms → mut after if OK (`share_nested_if_loop`; bad
`examples/bad/share_nested_if_loop.mko`). Match: share last-used on **all** arms →
mut after match OK (`share_match_arm`; bad `examples/bad/share_match_arm.mko`).
**Diverging match arms** (block that always `return`s / `break`s / `continue`s):
moves and share ends on that arm do not poison code after the match
(`hold_match_return_ok`; bad `share_match_return_live`, `hold_match_return_after`).
**CFG NLL (Done):** if/else joins moves (union of non-diverging arms) —
`hold_if_else_branch` / `examples/bad/hold_if_else_after.mko`; neither arm moves →
`hold_if_neither`. Match arms join the same way (`hold_match_arm` /
`hold_match_after`). While/`for` re-check the body only when some path can
**re-enter the header** (`continue` or fall-through) — always-`break` bodies skip
the second pass (`hold_always_break_ok`; still fail after move+break+use —
`hold_break_after_move`). Nested always-break does not poison an outer hold
(`hold_nested_break_ok`). **Const-bool edge prune:** `if false` / `while false` /
dead `continue` arms do not move (`hold_const_false_ok`); `if true` takes only
then (`examples/bad/hold_const_true_after.mko`). Loop-carried moves still fail
(`hold_loop_carried`, `hold_for_carried`, `hold_match_in_loop`).
**`break` / `continue`:** dead code after diverge (`hold_break_ok`,
`hold_continue_ok`). Continue-path moves join next iter / post-loop
(`hold_continue_carried`, `hold_continue_next_iter`, `hold_for_continue_carried`;
OK — `hold_continue_noll_ok`). Share joins / mid-scope / diverge arms unchanged
(`share_*` examples). Lambda/`fan` capture moves (`hold_lambda_ok` /
`hold_lambda_capture`). Implementation: `src/types/nll.rs` + joins in
`src/types/mod.rs`. Labeled `break`/`continue` shipped (`label: while` /
`break label`). Residual: share is not full RC graphs.

---

## 8. Concurrency & parallelism (first-class)

**Speed is the game.** Concurrent and parallel work are **language features**,
not packages — see [SPEED.md](SPEED.md).

Jobs cannot outlive their `crew` (no free-fire leaks, no async coloring).

```mko
// examples/concurrency.mko — task concurrency
fn main() {
    crew t {
        let a = t.kick(work(7))
        let b = t.kick(work(9))
        print(a.join() + b.join())
    }
}
```

```mko
// examples/parallel.mko — data parallelism across cores
fn main() {
    let xs = [1, 2, 3, 4]
    let ys = fan(xs, |x| x * x)           // pipe lambda
    let zs = fan(xs, fn(x) { x + 1 })     // Mako fn lambda
    for v in ys {
        print(v)
    }
}
```

```mko
// examples/cancel.mko — cooperative cancel
crew t {
    // t.cancel() / t.cancelled()
}
```

| Tool | Role |
|------|------|
| `crew` / `kick` / `join` | Structured concurrency; **join** returns the job’s type (`int`, `string`, `Result`, …) |
| `job.join_timeout(ms)` | Timed join → `Result[R, string]`: `Ok(value)` or `Err("timeout")` |
| `crew.drain(ms)` | Cancel + join with timeout |
| `fan(collection, mapper)` | Data-parallel map: `[]int` / `[]float` / `[]string` / `[]Struct` |
| channels + `select` | Message-passing: `make(chan[T], n)` / `chan_open[T](n)` for int/bool/float/string/**struct**/enum/**tuple** (incl. pack types) |
| `actor` / `receive` | Long-lived concurrent entities |

```mko
// string / Result across kick
fn greet() -> string { return "hi" }
fn open() -> Result[int, string] { return Ok(1) }

crew t {
    let a = t.kick(greet())
    let b = t.kick(open())
    print(a.join())
    match b.join() {
        Ok(v) => print_int(v),
        Err(e) => print(e),
    }
}
```

**Send-like kick args:** Copy scalars, `string`, deep-POD named structs
(scalar/string/nested POD fields), `Option`/`Result`/tuples of sendables,
channel handles. **Not** maps, arrays, arenas, or non-POD structs.

**Multi-field results:** prefer `chan[Struct]` (heap-box send) over packing
several ints into one integer. See [ERGONOMICS.md](ERGONOMICS.md) · [SPEED.md](SPEED.md).

Tests: `examples/testing/crew_fan_test.mko`, `job_join_typed_test.mko`,
`fan_struct_test.mko`, `kick_send_test.mko`, `chan_struct_test.mko`.

---

## 9. Channels and `select`

`make(chan[T], n)` and `chan_open[T](n)` accept the same element types: int
family, bool, float, string, **named structs**, **named enums**, and **tuples**
`(T, U[, …])` (including pack-qualified types).

```mko
let ch = chan_new(4)              // int
let cs = make(chan[string], 2)
let fs = chan_open[float](2)      // float (bitcast ring)
let ps = make(chan[Point], 2)     // same as chan_open[Point](2)
let pt = make(chan[(int, string)], 1)
// let pe = make(chan[eng.Table], 1)  // pack type after pull
let _ = ch.send(1)
let v = ch.recv()
ch.close()
// Depth / capacity work on every element type (not only chan[int]):
assert_eq(chan_len(ch), 0)
assert_eq(chan_cap(ps), 2)
```

`chan_len(ch)` / `chan_cap(ch)` accept **any** `chan[T]`. Struct, enum, and
tuple channels use the pointer-ring runtime; int/bool/float use the int ring;
string uses the string ring.

Tests: `chan_struct_test`, `chan_make_struct_test`, `chan_float_test`,
`chan_backpressure_test`, `lang_ergonomics_test` (`chan[tuple]`).

```mko
// examples/select_default.mko — timeout + default + up to 16 arms
// select / chan_select* : int or string arms (same family)
select timeout 30 {
    a => { print("got a") }
    b => { print("got b") }
    default => { print("default ok") }
}
```

Value: `chan_select_value()` (int) or `chan_select_value_str()` (string).
Helpers: `chan_select2` / `3` / `4`, `chan_str_select2`. Fairness: round-robin.
NLL: select arms and crew bodies keep hold-move joins.

---

## 10. Actors

```mko
// examples/actor.mko
actor Session {
    receive Invite { print("invite") }
    receive Timer { print("tick") }
    receive Bye { print("bye") }
}

fn main() {
    let session = Session_spawn()
    crew t {
        let loopj = t.kick(Session_loop(session))
        let _ = Session_send(session, Session_Invite())
        let _ = Session_send(session, Session_Bye())
        print_int(loopj.join())
    }
}
```

Desugars to mailbox + crew loop. `Bye` / `Stop` end the loop by convention.

---

## 11. Networking

### TCP

```mko
// examples/tcp_listen.mko, io_poll.mko
let fd = tcp_listen(18082)
let c = tcp_accept(fd)       // or tcp_accept_nb
let _ = tcp_write(c, "hi\n")
let _ = tcp_close(c)
let peer = tcp_connect("127.0.0.1", 18082)
```

### Upstream pool & reverse proxy

| Helper | Meaning |
|--------|---------|
| `tcp_pool_open` / `acquire` / `release` / `close` | Backend connection pool |
| `http_forward` | Forward → body only |
| `http_forward_full` / `http_forward_fd` | Status + body + headers (`HttpForwardResult`) |
| `http_proxy_raw` | Raw request/response byte pump |
| `http_parse` / `http_decode_chunked` | C hot-path parse / chunked decode |
| `tcp_connect_nb` / `tcp_fd_copy` | Nonblocking connect / efficient copy |

Full notes and edge cases: [BUILTINS.md](BUILTINS.md) *Reverse-proxy notes* · book
[ch08-networking](book/src/ch08-networking.md). Tests: `proxy_pool_test.mko`,
`proxy_edge_test.mko`.

### HTTP/1.1 server (Done — beachhead, not a framework)

Sync handler surface: `http_bind` → `http_accept` → read fields → `http_respond` →
`http_close` (one-shot) or `http_next` (keep-alive). Demos use a max-request loop +
`http_close_listener` so the process exits. Smoke: `./scripts/http-server-smoke.sh`.

| Helper | Meaning |
|--------|---------|
| `http_bind(port)` | listen socket |
| `http_accept(fd)` | accept + parse one request → conn id |
| `http_method` / `http_path` / `http_header` / `http_body` | request fields |
| `http_respond` / `http_respond_ct` | reply (ct sets Content-Type) |
| `http_close(c)` | free conn slot (use after one-shot respond) |
| `http_close_listener(fd)` | close listen socket (demo / graceful exit) |
| `http_next` / `http_keepalive` | keep-alive loop |
| `http_serve` / `http_echo` | fixed-body / echo helpers |
| `http_get(url)` | plain HTTP/1.0 GET client (non-TLS) |

```mko
// examples/http_server.mko — max N requests then exit (argv override)
fn main() {
    let mut max = 50
    let fd = http_bind(18100)
    let mut n = 0
    while n < max {
        let c = http_accept(fd)
        if c < 0 { /* skip */ } else {
            let p = http_path(c)
            if str_eq(p, "/health") {
                let _ = http_respond_ct(c, 200, "application/json", "{\"ok\":true}\n")
            } else {
                let _ = http_respond(c, 200, "hello from mako\n")
            }
            let _ = http_close(c)
            n = n + 1
        }
    }
    let _ = http_close_listener(fd)
}
```

```bash
mako build examples/http_server.mko -o out/http_server
out/http_server 5 &          # exit after 5 requests
curl -sS http://127.0.0.1:18100/health   # → {"ok":true}
```

Also: keep-alive (`examples/http_keepalive.mko`, `http_headers.mko`, `http_routes.mko`).
Runtime reaps dead keep-alive peers on `http_accept` so one-shot servers do not fill
the 32-slot conn table under curl.

### HTTPS listener wrap (Done — OpenSSL when linked)

`tls_serve_n(port, cert, key, body, max)` — max-N HTTPS demo: `/health` → JSON,
`/` → `body`, else 404; then exits. Soft-fails (returns 1) without OpenSSL.
Self-signed certs: `runtime/certs/dev.crt` + `dev.key`. Smoke skips when not linked:
`./scripts/https-server-smoke.sh`.

```mko
// examples/https_server.mko
let code = tls_serve_n(
    18443,
    "runtime/certs/dev.crt",
    "runtime/certs/dev.key",
    "hello from mako https\n",
    3
)
```

```bash
mako build examples/https_server.mko -o out/https_server
out/https_server &
curl -sk https://127.0.0.1:18443/health   # → {"ok":true}
```

Also: `tls_serve` / `tls_serve_once` (fixed body). Not a full HTTPS framework —
no middleware, no ACME, no production TLS policy surface.

### HTTP/2 TLS server (Done — OpenSSL + ALPN h2)

`tls_serve_h2_routes(port, cert, key, root_body, health_body, max)` — one TLS
connection, ALPN `h2`, route `/` / `/health`, up to `max` streams, then exit.
Smoke with `curl --http2` (skips without OpenSSL / HTTP/2 curl):
`./scripts/h2-server-smoke.sh`.

```mko
// examples/h2_server.mko
let code = tls_serve_h2_routes(
    18446,
    "runtime/certs/dev.crt",
    "runtime/certs/dev.key",
    "hello from mako h2\n",
    "{\"ok\":true}\n",
    2
)
```

```bash
mako build examples/h2_server.mko -o out/h2_server
out/h2_server &
curl -sk --http2 https://127.0.0.1:18446/health https://127.0.0.1:18446/
```

Also: `tls_serve_once_h2` / `tls_serve_h2_n`; gRPC-ish unary seed
`tls_serve_grpc_once` / `tls_grpc_unary` (not grpcurl-complete). Not a full H2/gRPC
server stack.

HTTP/2 seed (connection bootstrap + frames + 2-stream state; HPACK indexed/literal/dyn/Huffman):
`http2_client_preface` / `http2_server_preface` / `http2_settings_ack` /
`http2_settings_max_concurrent` (SETTINGS id 0x3) /
`http2_is_settings_ack`, `http2_detect`, `http2_empty_settings`, `http2_frame_*`,
`http2_headers_frame` / `http2_data_frame` / `http2_continuation_frame`,
`http2_stream_apply` / `http2_stream_apply_local` / `http2_stream_state_of` /
`http2_stream_half_closed_remote` / `http2_stream_half_closed_local` /
`http2_conn_reset` / `http2_conn_recv` (HEADERS/DATA/RST SM + CONTINUATION assembly until END_HEADERS;
non-ACK SETTINGS sets ack-needed; MAX_CONCURRENT_STREAMS enforced on new HEADERS;
inbound DATA decreases stream+conn windows; reject DATA on idle / HEADERS on closed) /
`http2_conn_settings_ack_needed` / `http2_conn_auto_settings_ack` (emit ACK frame + clear flag) /
`http2_conn_pump` (recv multi-frame buffer + auto SETTINGS ACK and/or PING ACK if owed) /
`http2_conn_goaway_last` (last-stream after GOAWAY; new streams > last rejected) /
`http2_conn_max_concurrent` / `http2_conn_active_streams` /
`http2_conn_set_server` / `http2_conn_is_server` (stream-id parity on new HEADERS) /
`http2_conn_header_block` / `http2_conn_header_stream` / `http2_conn_header_assembling` /
`http2_conn_preface_received` / `http2_conn_settings_exchanged` /
`http2_conn_closing`,
`http2_window_of` / `http2_window_conn` / `http2_window_blocked` /
`http2_window_consume` / `http2_window_increment`,
`http2_goaway_frame` / `http2_ping_frame` / `http2_window_update_frame` /
`http2_rst_stream_frame` / `http2_priority_frame` / `http2_push_promise_frame` /
`http2_is_goaway` / `http2_is_ping` / `http2_is_window_update` /
`http2_is_rst_stream` / `http2_is_priority` / `http2_is_push_promise` /
`http2_goaway_last_stream` / `http2_goaway_error` / `http2_window_update_increment` /
`http2_rst_stream_error` / `http2_priority_dep` / `http2_priority_weight` /
`http2_priority_exclusive` / `http2_priority_apply` /
`http2_stream_priority_dep` / `http2_stream_priority_weight` /
`http2_stream_priority_exclusive` / `http2_stream_priority_child_count` /
`http2_schedule_next` (tree-aware: parent_weight×256+own; tie → lowest id) /
`http2_conn_recv` also applies PRIORITY (exclusive reparent); RST aborts incomplete CONTINUATION /
`http2_push_promise_stream` /
`http2_header_block` (HEADERS+CONTINUATION merge),
`hpack_*` / `hpack_huffman_encode` / `hpack_huffman_decode` /
`hpack_decode_block` / `hpack_decoded_count` / `hpack_decoded_name` /
`hpack_decoded_value` / `hpack_decode_clear` (indexed + literal-new-name).
QUIC: `quic_detect` (long) / `quic_long_header` / `quic_short_header` /
`quic_long_type` (0=Initial…3=Retry) / `quic_is_retry` /
`quic_is_version_negotiation` / `quic_vn_version_count` / `quic_vn_version_at` /
`quic_vn_select` (preferred or first) /
`quic_has_crypto` / `quic_crypto_offset` /
`quic_crypto_data_offset` / `quic_crypto_data_len` / `quic_crypto_data`
(unprotected Initial CRYPTO) /
`quic_crypto_frame` / `quic_crypto_payload` / `quic_payload_crypto_data` /
`quic_payload_crypto_data_len` (build CRYPTO+PADDING; parse after decrypt) /
`quic_ack_frame` / `quic_ack_largest` / `quic_ack_delay` / `quic_ack_range_count` /
`quic_ack_first_range` / `quic_ack_smallest` / `quic_is_ack`
(ACK type=0x02 single-range seed; 1-byte varints only) /
`quic_stream_frame` / `quic_is_stream` / `quic_stream_id_of` / `quic_stream_offset` /
`quic_stream_fin` / `quic_stream_data` / `quic_stream_data_len`
(STREAM type 0x08–0x0f seed; 1-byte varints) /
`quic_spin_bit` / `quic_key_phase` /
`quic_version` / `quic_dcid_len` / `quic_dcid` /
`quic_scid_len` / `quic_scid` / `quic_payload_offset` /
`quic_hkdf_expand_label` / `quic_hkdf_expand_label_hex`
(HKDF-Expand-Label demo for initial secrets — **not** full QUIC protection) /
`quic_initial_client_secret` / `quic_initial_client_secret_hex` /
`quic_initial_client_key` / `quic_initial_client_iv` / `quic_initial_client_hp` (+ `_hex`) /
`quic_initial_protect` / `quic_initial_unprotect`
(AES-GCM payload AEAD from initial client secret; AAD = caller header) /
`quic_header_protection_mask` / `quic_initial_hp_mask` (+ `_hex`) /
`quic_header_protect_apply` / `quic_header_protect_remove` /
`quic_initial_packet_protect` / `quic_initial_packet_unprotect`
(minimal protected Initial assemble/decode; CRYPTO-in-payload roundtrip seed —
not full A.2 1162-byte CRYPTO/PADDING sample) /
`hex_decode` (RFC 9001 §5.2 from dest CID + v1 salt; Appendix A.1 key/iv/hp KATs).
TLS record: `tls_record_*`.
Buffer AEAD: `tls_aead_seal` / `tls_aead_open` (AES-128-GCM; not record AEAD).
Record seed: `tls_record_appdata_seal` / `tls_record_appdata_open`
(type=23 + 0x0303 header; AAD = 5-byte header).
Seq nonce: `tls_record_appdata_seal_seq` / `open_seq` (IV XOR seq into last 8 bytes;
`tls_record_seq_reset` / `write_seq` / `read_seq`).
Handshake seed: `tls_client_hello` / `tls_client_hello_random` /
`tls_client_hello_legacy_version` / `tls_client_hello_has_aes128_gcm` /
`tls_server_hello` / `tls_server_hello_random` /
`tls_encrypted_extensions` / `tls_finished` /
`tls_certificate` / `tls_certificate_der` /
`tls_certificate_verify` / `tls_certificate_verify_scheme` / `tls_certificate_verify_sig` /
`tls_finished_verify_data` / `tls_finished_verify_data_hex` /
`tls_transcript_reset` / `tls_transcript_append` / `tls_transcript_len` /
`tls_transcript_finished_hex` /
`tls_hs_reset` / `tls_hs_state` / `tls_hs_advance` / `tls_hs_is_app` / `tls_hs_msg_type` /
`tls_hs_session_reset` / `tls_hs_session_feed` /
`tls_hs_session_client_hello` / `tls_hs_session_server_hello` /
`tls_hs_session_encrypted_extensions` / `tls_hs_session_certificate` /
`tls_hs_session_certificate_verify` / `tls_hs_session_finished` /
`tls_hs_session_finished_hex`
(session wires CH→…→Finished into SM + transcript — not a live handshake) /
`tls_derive_secret` / `tls_derive_secret_hex` /
`tls_client_handshake_traffic_secret` / `tls_server_handshake_traffic_secret` (+ `_hex`) /
`tls_client_application_traffic_secret` / `tls_server_application_traffic_secret` (+ `_hex`)
(Derive-Secret / traffic-secret demos — not a live handshake).

Protobuf / gRPC wire seed: `pb_encode_*` / `pb_simple_*` / nested / repeated,
`grpc_encode_message` / `grpc_message_*` / `grpc_message_within_limit` /
`grpc_default_max_message` (4 MiB default), `grpc_unary_*`,
`grpc_http2_unary` / `grpc_http2_unary_payload` /
`grpc_http2_unary_response` / `grpc_http2_unary_response_status` /
`grpc_http2_response_payload` /
`grpc_http2_response_status` (HEADERS+DATA+trailer HEADERS buffer → grpc-status; trailer-only → -1),
`grpc_http2_stream_data` / `grpc_http2_stream_two` / `grpc_http2_stream_data_count`
(streaming DATA with/without END_STREAM seed),
`grpc_http2_client_stream_flow` (two client DATA + server response/status one-flow seed),
`grpc_content_type` / `grpc_status_trailer` / `grpc_status_code`.
Round-trip example: `examples/grpc_unary_roundtrip.mko`.

Postgres (libpq when linked): `pg_connect` / `pg_ok` / `pg_exec` / `pg_close` /
`pg_exec_row_count` / `pg_connect_url`
— `pg_ok==0` when server down or no libpq (never fake success);
`pg_exec` / `pg_exec_row_count` return -1 when disconnected; `pg_close` is a no-op on handle 0;
`pg_connect_url` parses `postgres://…` to libpq keywords (no connect).

### TLS

**Production integration started** (Homebrew OpenSSL when `MAKO_HAS_OPENSSL`):
live handshake + verified GET/POST + ALPN h2 GET — not a full TLS/HTTP2 stack yet.
See [TLS_LIVE.md](TLS_LIVE.md) for opt-in live tests (`MAKO_LIVE_TLS=1`).

```mko
// Live path (examples/testing/tls_live_test.mko — set MAKO_LIVE_TLS=1):
//   tls_serve_once / tls_serve_n / tls_serve_once_h2
//   tls_serve_n — max-N HTTPS demo (/health JSON); examples/https_server.mko
//   tls_handshake_ok / tls_handshake_version  — SSL_connect + peer verify
//   tls_get / tls_post / tls_get_insecure     — HTTPS/1.1
//   tls_h2_settings_exchange                  — ALPN h2 + SETTINGS
//   tls_h2_get / tls_h2_post                  — HEADERS+DATA, returns "200\\n<body>"
//   tls_h2_get_twice / tls_serve_h2_n         — keep-alive second request (streams 1+3)
//   tls_h2_mux / tls_serve_h2_routes          — overlapping `/` + `/health` multiplex
//   tls_serve_grpc_once / tls_grpc_unary      — gRPC-ish unary over h2 (echo + grpc-status)
//   tls_serve_grpc_stream / tls_grpc_stream   — 2 client DATA → 2 echoed DATA + trailer
//   tls_serve_h2_wu / tls_h2_window_get       — WINDOW_UPDATE on wire + GET continues
//   nghttp2_available / nghttp2_get / nghttp2_post / nghttp2_get_two — real libnghttp2 (when linked)
//   quiche_available / quiche_version / quiche_handshake / quiche_h3_get / quiche_h3_post / quiche_h3_get_two
// Demos: examples/tls_once.mko, tls_verify.mko, tls_client.mko
// Certs: runtime/certs/dev.crt (CN=localhost), runtime/certs/dev.key
// Next: runtime/third_party (nghttp2 linked; quiche needs Rust FFI)
```

Secure defaults (OpenSSL path): TLS 1.2+, modern cipher suites, no compression.
`tls_get` / `tls_post` verify peers; `tls_get_insecure` skips verify for local demos.
When `libnghttp2` is detected, prefer `nghttp2_get` / `nghttp2_post` for production-shaped h2 clients.

### Building APIs

Mako is a **backend language**: HTTP handlers, JSON, routing, and service templates.
**How-to:** [howto/02-http-apis.md](howto/02-http-apis.md) · **STDLIB:** [STDLIB.md](STDLIB.md).

```bash
mako init mysvc --backend    # scaffold main.mko + README
# or study: examples/api_backend/ · examples/http_lib/
```

HTTP library (HTTP/1.1 builtins):

| Builtin | Role |
|---------|------|
| `http_bind` / `http_accept` / `http_close` | Listen loop |
| `http_method` / `http_path` / `http_body` / `http_header` | Request |
| `http_respond` / `http_respond_ct` / `http_respond_json` | Response |
| `http_get` / `http_post` / `http_request` | Client |
| `http_get_timeout` / `http_post_timeout` | Client + timeout |
| `http_last_status` / `http_last_header` | Last client response meta |
| `json_*` / `json_object_from_map_ss` | Encode/decode glue |

```mko
let c = http_accept(fd)
if str_eq(http_path(c), "/health") {
    let _ = http_respond_json(c, 200, "{\"ok\":true}\n")
}
let _ = http_close(c)
let body = http_get("http://127.0.0.1:8080/health")
print_int(http_last_status())
```

Smoke: `./scripts/http-lib-smoke.sh` · HTTPS/H2: `examples/https_server.mko`, `examples/h2_server.mko`.
### Direct I/O (`mako_dio.h`)

Low-level unbuffered file operations and memory-mapped files for storage engines,
databases, and performance-critical I/O paths. Runtime: `runtime/mako_dio.h`.

```mko
// examples/dio_write.mko — positional read/write without buffering
fn main() {
    let fd = file_open("/tmp/mako_dio.dat", 1, 0)  // mode=write
    let _ = fallocate(fd, 4096)                     // pre-allocate space
    let _ = pwrite(fd, "hello dio", 0)              // write at offset 0
    let _ = fdatasync(fd)                           // flush data to disk
    let _ = file_close(fd)

    let fd2 = file_open("/tmp/mako_dio.dat", 0, 0)  // mode=read
    let data = pread(fd2, 9, 0)                      // read 9 bytes at offset 0
    print(data)                                      // "hello dio"
    print_int(file_size(fd2))
    let _ = file_close(fd2)
}
```

| Op | Signature | Notes |
|----|-----------|-------|
| `file_open(path, mode, flags)` | `-> int` | Open fd; mode 0=read, 1=write, 2=rw |
| `file_close(fd)` | `-> int` | Close fd |
| `pread(fd, count, offset)` | `-> string` | Positional read (no seek) |
| `pwrite(fd, data, offset)` | `-> int` | Positional write (no seek) |
| `file_append(fd, data)` | `-> int` | Append to file |
| `fsync(fd)` | `-> int` | Flush data + metadata |
| `fdatasync(fd)` | `-> int` | Flush data only |
| `fallocate(fd, size)` | `-> int` | Pre-allocate disk space |
| `file_size(fd)` | `-> int` | File size in bytes |
| `file_truncate(fd, size)` | `-> int` | Truncate file |
| `file_seek(fd, offset, whence)` | `-> int` | Seek; whence 0=SET,1=CUR,2=END |
| `file_read_exact(fd, n)` | `-> string` | Read exactly n bytes or fail |

#### Memory-mapped files (MMap)

```mko
// examples/mmap_kv.mko — memory-mapped file for fast random access
fn main() {
    let m = mmap_create("/tmp/mako.mmap", 4096)
    let _ = mmap_write(m, 0, "key1=value1\n")
    let _ = mmap_sync(m, 0)
    let data = mmap_read(m, 0, 12)
    print(data)                       // "key1=value1\n"
    print_int(mmap_size(m))           // 4096
    let _ = mmap_close(m)

    // Re-open existing mapping
    let m2 = mmap_open("/tmp/mako.mmap", 0)
    let data2 = mmap_read(m2, 0, 12)
    print(data2)
    let _ = mmap_close(m2)
}
```

| Op | Signature | Notes |
|----|-----------|-------|
| `mmap_open(path, mode)` | `-> MMap` | Map existing file |
| `mmap_create(path, size)` | `-> MMap` | Create + map new file |
| `mmap_read(m, offset, count)` | `-> string` | Read from mapping |
| `mmap_write(m, offset, data)` | `-> int` | Write to mapping |
| `mmap_sync(m, flags)` | `-> int` | Flush mapping to disk |
| `mmap_size(m)` | `-> int` | Size of mapping |
| `mmap_close(m)` | `-> int` | Unmap and close |

---

### Binary Buffer (`Buf` type)

The `Buf` type provides structured reading and writing of binary data — useful for
network protocols, file formats, and serialization. Runtime: `runtime/mako_buf.h`.

```mko
// examples/buf_protocol.mko — build and parse a binary message
fn main() {
    // Pack a message: u8 type + u32 length + payload
    let b = buf_pack_new(64)
    buf_write_u8(b, 1)                 // message type
    buf_write_u32(b, 5)                // payload length
    buf_write_str(b, "hello")          // payload
    let wire = buf_to_string(b)
    buf_free(b)

    // Parse it back
    let r = buf_from_string(wire)
    let msg_type = buf_read_u8(r)
    let msg_len = buf_read_u32(r)
    let payload = buf_read_str(r, msg_len)
    print_int(msg_type)                // 1
    print_int(msg_len)                 // 5
    print(payload)                     // "hello"
    buf_free(r)
}
```

| Op | Notes |
|----|-------|
| `buf_pack_new(capacity)` | New write buffer |
| `buf_from_string(s)` | Buffer from existing bytes |
| `buf_to_string(b)` | Extract contents as string |
| `buf_len(b)` / `buf_pos(b)` | Total length / current position |
| `buf_reset(b)` / `buf_seek(b, pos)` | Reset to start / seek to position |
| `buf_free(b)` | Free buffer memory |
| `buf_write_u8/u16/u32/u64` | Write unsigned integers (little-endian) |
| `buf_write_i32/f32/f64` | Write signed int / floats |
| `buf_write_u16be/u32be` | Write big-endian unsigned |
| `buf_read_u8/u16/u32/u64` | Read unsigned integers (little-endian) |
| `buf_read_i32/f32/f64` | Read signed int / floats |
| `buf_read_u16be/u32be` | Read big-endian unsigned |
| `buf_write_bytes(b, data)` / `buf_write_str(b, s)` | Write raw bytes/string |
| `buf_read_bytes(b, n)` / `buf_read_str(b, n)` | Read n raw bytes/string |

---

### Systems programming

Ownership (`hold`/`share`), arenas, bytes, and files — no mandatory GC:

| Pattern | Example |
|---------|---------|
| Arenas | `examples/arena.mko`, `arena_struct.mko` |
| Hold / NLL | `examples/hold_*.mko`, `docs/LANGUAGE.md` |
| Bytes / FS | `examples/bytes.mko`, `file_env.mko`; `append_file` for logs |
| Append log | `examples/systems_log/` |
| FFI | `examples/extern_c.mko` |

### Building databases / storage engines

Beyond SQL **clients** (SQLite/Postgres): ship a mini **embedded** engine in Mako.

| Piece | Path |
|-------|------|
| Append-only KV (PUT/DEL log, replay) | `examples/db_engine/` |
| Tests | `examples/testing/db_engine_test.mko` |
| SQL clients | §14 below |

```bash
mako run examples/db_engine/main.mko
mako test examples/testing/db_engine_test.mko
```

Buffer AEAD (AES-128-GCM via OpenSSL — **not** TLS record AEAD / handshake):
`tls_aead_seal(key16, nonce12, plaintext, aad)` → ciphertext||tag(16);
`tls_aead_open(...)` → plaintext or empty on auth fail
(`examples/testing/tls_aead_test.mko`).

TLS 1.3-like application_data record seed (header + AEAD, AAD = 5-byte header):
`tls_record_appdata_seal` / `tls_record_appdata_open`;
seq variants `tls_record_appdata_seal_seq` / `open_seq`.
Handshake **seed** helpers (`tls_client_hello` / hs_session / traffic secrets) remain
unit-test seeds — prefer the live OpenSSL builtins above for integration.

### WebSocket

```mko
// examples/ws_echo.mko, ws_ping.mko
// RFC6455 upgrade + text; ping/pong + binary
```

---

## 11b. Event Loop (Non-blocking I/O)

Mako provides a high-performance event loop for non-blocking I/O multiplexing
(uses epoll on Linux, kqueue on macOS under the hood). Runtime: `runtime/mako_evloop.h`.

### Event Loop Core

```mko
// Create and use an event loop
let el = evloop_new()
let server_fd = nb_listen(8080)
let _ = evloop_add(el, server_fd, 1)  // 1 = readable

while true {
    let n = evloop_wait(el, 1000)  // wait up to 1000ms
    let mut i = 0
    while i < n {
        let fd = evloop_event_fd(el, i)
        let flags = evloop_event_flags(el, i)
        if fd == server_fd {
            let client = nb_accept(server_fd)
            let _ = evloop_add(el, client, 1)
        } else {
            let data = nb_read(fd)
            let _ = nb_write(fd, "HTTP/1.1 200 OK\r\n\r\nhi\n")
            let _ = evloop_del(el, fd)
            let _ = nb_close(fd)
        }
        i = i + 1
    }
}
let _ = evloop_close(el)
```

| Function | Purpose |
|----------|---------|
| `evloop_new()` | Create event loop |
| `evloop_add(el, fd, flags)` | Register fd for monitoring |
| `evloop_mod(el, fd, flags)` | Modify interest flags |
| `evloop_del(el, fd)` | Remove fd from monitoring |
| `evloop_wait(el, timeout_ms)` | Wait for events, returns count |
| `evloop_event_fd(el, index)` | Get fd from ready event |
| `evloop_event_flags(el, index)` | Get flags from ready event |
| `evloop_close(el)` | Destroy event loop |

### Non-blocking Socket Helpers

| Function | Purpose |
|----------|---------|
| `nb_listen(port)` | Non-blocking TCP listener |
| `nb_accept(fd)` | Non-blocking accept |
| `nb_read(fd)` | Non-blocking read |
| `nb_write(fd, data)` | Non-blocking write |
| `nb_udp_bind(port)` | Non-blocking UDP socket |
| `nb_udp_recv(fd)` | Non-blocking UDP receive |
| `nb_close(fd)` | Close non-blocking socket |

---

## 11c. Game UDP

High-performance UDP networking for game servers. Runtime: `runtime/mako_game.h`.

```mko
let u = game_udp_bind(27015)
let el = evloop_new()
let _ = evloop_add(el, game_udp_fd(u), 1)

while true {
    let n = evloop_wait(el, 16)  // ~60 Hz
    if n > 0 {
        let data = game_udp_recv(u)
        let peer = game_udp_sender(u)
        let _ = game_udp_broadcast(u, "state:" + data)
    }
}
game_udp_close(u)
```

| Function | Purpose |
|----------|---------|
| `game_udp_bind(port)` | Bind UDP game socket |
| `game_udp_recv(u)` | Receive packet (tracks sender) |
| `game_udp_sender(u)` | Get peer ID of last sender |
| `game_udp_send(u, peer, data)` | Send to specific peer |
| `game_udp_broadcast(u, data)` | Send to all connected peers |
| `game_udp_kick(u, peer)` | Disconnect a peer |
| `game_udp_peers(u)` | Number of connected peers |
| `game_udp_fd(u)` | Raw fd for event loop integration |
| `game_udp_close(u)` | Close socket |
| `tick_now_us()` | Microsecond timestamp for game ticks |
| `tick_sleep_us(start_us, interval_us)` | Sleep to maintain tick rate |

---

## 11d. Cloud / Distributed Primitives

Primitives for building distributed services. Runtime: `runtime/mako_cloud.h`.

### Consistent Hashing

```mko
let ring = chash_new(3, 150)  // 3 nodes, 150 virtual nodes each
let node = chash_get(ring, "user:42")
print_int(node)  // 0, 1, or 2

let new_id = chash_add_node(ring)
chash_remove_node(ring, 0)
print_int(chash_node_count(ring))  // 3
chash_free(ring)
```

| Function | Purpose |
|----------|---------|
| `chash_new(nodes, vnodes)` | Create hash ring |
| `chash_get(ring, key)` | Get node for key |
| `chash_add_node(ring)` | Add node, returns ID |
| `chash_remove_node(ring, node)` | Remove node |
| `chash_node_count(ring)` | Active node count |
| `chash_free(ring)` | Destroy ring |

### Rate Limiter

```mko
let rl = ratelimit_new(100, 10)  // 100 tokens/sec, burst of 10
if ratelimit_allow(rl) == 1 {
    // request allowed
}
print_int(ratelimit_remaining(rl))
ratelimit_free(rl)
```

| Function | Purpose |
|----------|---------|
| `ratelimit_new(rate, burst)` | Token bucket limiter |
| `ratelimit_allow(r)` | Consume token (1=allowed, 0=rejected) |
| `ratelimit_remaining(r)` | Tokens remaining |
| `ratelimit_free(r)` | Destroy |

### Circuit Breaker

```mko
let cb = breaker_new(5, 30000, 3)  // 5 failures, 30s timeout, 3 half-open max
if breaker_allow(cb) == 1 {
    // attempt request
    breaker_success(cb)
} else {
    // circuit is open, fail fast
}
print_int(breaker_state(cb))  // 0=closed, 1=open, 2=half-open
breaker_free(cb)
```

| Function | Purpose |
|----------|---------|
| `breaker_new(threshold, timeout_ms, half_open_max)` | Create circuit breaker |
| `breaker_allow(cb)` | Check if request should proceed |
| `breaker_success(cb)` | Record success |
| `breaker_failure(cb)` | Record failure |
| `breaker_state(cb)` | 0=closed, 1=open, 2=half-open |
| `breaker_reset(cb)` | Reset to closed |
| `breaker_free(cb)` | Destroy |

---

## 11e. HTTP Engine

High-level HTTP server with declarative routing. Runtime: `runtime/mako_httpengine.h`.

```mko
let e = httpengine_new()
let _ = httpengine_route(e, "GET", "/health", 1)
let _ = httpengine_route(e, "POST", "/api/users", 2)
let _ = httpengine_start(e, 8080)
// Engine runs until stopped
httpengine_stop(e)
httpengine_free(e)
```

| Function | Purpose |
|----------|---------|
| `httpengine_new()` | Create HTTP engine |
| `httpengine_route(e, method, path, handler_id)` | Register route |
| `httpengine_start(e, port)` | Start listening |
| `httpengine_stop(e)` | Stop engine |
| `httpengine_free(e)` | Destroy engine |

---

## 12. Async I/O (colorless)

Ordinary functions; runtime uses `select` / `kqueue` / `epoll` underneath.
**No `async`/`await` keywords.**

```mko
// examples/io_wait.mko
let ready = io_wait(fd, 50)          // 1 ready, 0 timeout

// examples/io_poll.mko / io_poll4.mko
let which = io_poll2(a, b, 500)      // index or -1
let which4 = io_poll4(a, b, c, d, 40)

// examples/io_kq.mko / io_native.mko
let w = io_kq_poll2(a, b, 500)       // Darwin kqueue
let n = io_native_poll2(a, b, 500)   // kqueue | epoll | select
```

Linux `epoll` is compiled under `#ifdef __linux__` (`io_epoll_poll2`).

---

## 13. JSON and `#[derive(json)]`

```mko
// examples/derive_json.mko
#[derive(json)]
struct Person {
    name: string
    age: int
}

fn main() {
    let j = Person_to_json("Ada", 36)
    let name = Person_name_from_json(j)
    let age = Person_age_from_json(j)
}
```

```mko
// examples/json_nested.mko, json_array.mko, json_map.mko
let nested = json_nest("addr", addr)
let doc = json_merge(person, nested)
let city = json_path_string(doc, "addr", "city")
let nums = json_array_ints3(10, 20, 30)
let more = json_array_push_int(nums, 40)
let names = json_array_push_string(json_array_strings2("a", "b"), "c")
let mut m = make(map[string]string, 4)
m["k"] = "v"
let obj = json_object_from_map_ss(m)
```

Also: `json_object`, `json_si` / `json_ss` / `json_i`, `json_get_string` /
`json_get_int`, `json_get_object`, `json_array_len`, `json_array_get_*`.

`const` comptime fold: `const PORT = 10 * 100 + 80 / 2` (`examples/derive_json.mko`).

---

## 14. SQLite and Redis

```mko
// examples/sqlite.mko — needs libsqlite3
let n = sqlite_query_int("file.db", "select 42")
let s = sqlite_query_text("file.db", "select 'hi'")
```

```mko
// examples/redis_ping.mko — RESP PING; mock if no redis-server
crew t {
    let s = t.kick(redis_mock_once(16379))
    sleep_ms(40)
    let r = redis_ping("127.0.0.1", 16379)  // "PONG"
    let _ = s.join()
}
// SET/GET/DEL/EXISTS: redis_set / redis_get / redis_del / redis_exists + redis_mock_kv
// (examples/testing/redis_kv_test.mko)
```

Unified SQL facade (`sql_open_sqlite` / `sql_open_postgres` → `SqlDB`):

```mko
let db = sql_open_sqlite("/tmp/app.db")

// DDL or simple statements (no parameters)
let _ = sql_exec_plain(db, "CREATE TABLE IF NOT EXISTS users(id INTEGER PRIMARY KEY, name TEXT, email TEXT)")

// Insert with up to 4 string parameters ($1..$4 for Postgres, ? for SQLite)
let _ = sql_exec_str4(db, "INSERT INTO users(name, email, '', '') VALUES ($1, $2, $3, $4)", "Ada", "ada@example.com", "", "")

// Query a single string value (first column, first row)
let name = sql_query_str(db, "SELECT name FROM users WHERE email = $1", "ada@example.com")
print(name)  // Ada

sql_close(db)
```

`sql_exec_plain` returns 0 on success; `sql_exec_str4` returns 0 on success (uses parameterized
queries for safety); `sql_query_str` returns `""` if no rows match. These complement
`sql_exec(db, sql, []int)` which only supports integer parameters.

Postgres: libpq `pg_connect` is available (fails gracefully when server down).
`pg_connect_url` parses URLs; `pg_exec_row_count` returns -1 when disconnected.

Live Docker fixtures (ephemeral; tear down after):
`scripts/ci-redis.sh` → `examples/ci/redis_live.mko` (PING/SET/GET/DEL on `:6380`);
`scripts/ci-postgres.sh` → `examples/ci/pg_live.mko` (`pg_ok`/`pg_exec` on `:5433`).
Default suite still uses mocks / down-server asserts (no Docker required).

Time / path / file / env / args / log helpers (stdlib — full index: [STDLIB.md](STDLIB.md)):

```mko
// examples/stdlib/demo.mko · testing/stdlib_*_test.mko · path_join.mko · …
let p = path_join("foo", "bar")  // "foo/bar"
assert_eq_str(path_clean("/a/../b"), "/b")
assert_eq_str(str_join(str_split("a,b", ","), "-"), "a-b")
assert_eq_str(fmt_sprintf("hi %s", "mako"), "hi mako")
let t = now_ms()                 // wall clock ms
let ns = now_ns()                // monotonic ns (benches)
let x = black_box(x)             // prevent LTO from erasing bench work
sleep_ms(1)
print(time_format(t))            // RFC3339 UTC
let _ = mkdir("/tmp/d")
let _ = write_file("/tmp/d/x.txt", "hi")
assert(file_exists("/tmp/d/x.txt"))
assert(is_dir("."))
let s = read_file("/tmp/d/x.txt")
let _ = remove_file("/tmp/d/x.txt")
let _ = env_set("K", "v")
let v = env_get("K")
print_int(argc())
print(arg_get(0))
let a = args()                   // []string of argv
log_info("hello")                // "[<now_ms> info] hello"
log_warn("careful")
log_error("boom")
log_kv("info", "user", "ada")
match parse_int("42") { Ok(v) => print_int(v), Err(_) => {} }
print_float(parse_float("3.5"))
print(base64_encode("hi"))
print(hex_encode("AB"))
assert_eq(abs(-3), 3)
assert(ints_contains(sort_ints([3, 1, 2]), 1))
let xs = sort_ints([3, 1, 2])
let ss = sort_strings(["c", "a", "b"])
assert(regex_match("^hi$", "hi"))
assert(regex_match("a.c", "abc"))      // `.` = any char
assert(regex_match("ab*c", "abbbc"))   // `*` = greedy star on prior atom
assert(regex_match("ab+c", "abbbc"))   // `+` = one or more
assert(regex_match("ab?c", "ac"))      // `?` = zero or one
assert(regex_match("cat|dog", "dog"))  // `|` alternation
assert(regex_match("[a-z]+", "hi"))    // character class + range
assert(regex_match("[^0-9]+", "hi"))   // negated class
assert(regex_match("(ab)+c", "ababc")) // grouping
print(regex_capture("a(b+)c", "xxabbbc", 1))  // "bbb" — group 1; 0 = full match
print(regex_find("ab", "xxabyy"))
print(regex_find("a.*c", "xxabyczz"))
let t0 = now_ms()
time_sleep_ms(1)                       // alias of sleep_ms
print_int(elapsed_ms(t0))
// exit(7)                             // process exit with status code
```

Regex seed supports literals, `.`, `X*`/`X+`/`X?`, `|`, `[abc]`/`[a-z]`/`[^…]`, `(…)` groups, and `^`/`$`. Groups compose only (no `$1` extraction).

### Pulls (Mako packs)

Normal pulls always bind a **pack name** used as a qualifier. That keeps call
sites clear — with Mako flair (see [IDENTITY.md](IDENTITY.md)).

```mko
// Default name from `pack lib` in the pulled file (or last path segment)
pull "./import_lib.mko"
print(lib.add(2, 3))

// Explicit alias
pull "./import_ns_lib.mko" as lib

// Dual still accepted: import … · import lib "path"

// Nested std paths
pull "strings"
pull "encoding/json"
pull "net/http"
assert(strings.contains("hi", "h"))
print(json.object_str("k", "v"))

// Large grouped list — blank lines separate std / remote / local (like goimports)
import (
    "fmt"
    "net/http"
    "strings"

    "github.com/sirupsen/logrus"

    redisv8 "github.com/go-redis/redis/v8"
    "izi-iva/pkg/acd"
)

// Blank — load only, no names
pull _ "fmt"

// Dot — merge without prefix (specialized; use sparingly)
pull . "./import_lib.mko"
```

| Form | Meaning |
|------|---------|
| `pull "strings"` | Std pack → `strings.*` |
| `pull "encoding/json"` | Nested std → default name `json` |
| `pull "path" as lib` | Explicit alias → `lib.*` |
| `import name "path"` | Dual alias form |
| `import ( … )` / `pull ( … )` | Grouped list (blank lines ok) |
| `pull _ "path"` | Blank: dependency only |
| `pull . "path"` | Dot: no prefix (specialized) |

**Resolution order** for a path like `"izi-iva/pkg/acd"` or `"github.com/…"`:

1. Relative (`./`, `../`, `.mko`)
2. Std under `std/`
3. `[dependencies]` key matching the import path
4. `module = "izi-iva"` in `mako.toml` → strip prefix, resolve under project root
5. `vendor/<path>/`
6. `.mako/pkg/<path>/`

See `examples/import_paths/` for a full service-style import block.  
`mako fmt` groups into `pull ( … )` with blank lines between std / remote / relative.

`mako run prog.mko -- arg1 arg2` forwards args to the program.

---

## 15. Testing

| Feature | Syntax |
|---------|--------|
| Test file | `foo_test.mko` (same dir as code) |
| Test function | `fn TestAdd() { … }` |
| Run all | `mako test [path]` |
| Filter | `mako test --run TestAdd` or `-r 'Test*'` or `-r '/^TestAdd$/'` |
| Verbose | `mako test -v` / `--verbose` (lists matched test functions) |
| Repeat | `mako test --count N` |
| Coverage | `mako test --coverage` |
| Categories | `fn FuzzXxx()` / `PropertyXxx()` / `SnapshotXxx()` / `MockXxx()` / `FixtureXxx()` |
| Subtests | `t_run("name")` · nest with `t_run_nested("child")` → `Parent/child` |

```mko
// examples/testing/add.mko
fn add(a: int, b: int) -> int {
    return a + b
}

// examples/testing/add_test.mko
fn TestAdd() {
    assert_eq(add(2, 3), 5)
}

fn TestAddTable() {
    let a = [1, 2, 10]
    let b = [1, 3, 5]
    let want = [2, 5, 15]
    for i in 3 {
        assert_eq(add(a[i], b[i]), want[i])
    }
}
```

```mko
// examples/testing/sub_test.mko
fn TestSubs() {
    t_run("ok")
    assert_eq(1, 1)
    t_run("also")
    assert_eq(2, 2)
}

fn TestNested() {
    t_run("outer")
    assert_eq(1, 1)
    t_run_nested("inner")   // prints TestNested/outer/inner
    assert_eq(2, 2)
}
```

Helpers: `assert`, `assert_eq`, `assert_eq_str`, `fail("msg")`. A failed assert
fails the current test (or subtest) and continues to the next. Exit code is
non-zero if any test failed.

```bash
cargo run --release -- test examples/testing
cargo run --release -- test examples/testing --run TestAdd
cargo run --release -- test examples/testing -r TestSubs
cargo run --release -- test examples/testing -v
cargo run --release -- test examples/testing -v -r 'TestAdd*'
cargo run --release -- test examples/testing -r '/^TestAdd$/'
cargo run --release -- test examples/testing -r '/Add|Mul/'
```

`-run` / `-r` matching (in order):

1. `/pattern/` — Rust `regex` (unanchored unless you write `^`/`$`); invalid pattern matches nothing
2. `*` / `?` glob
3. otherwise substring

`-v` prints `run: TestA, TestB` before compiling each matched file.

---

## 16. Tooling

```bash
mako check path.mko          # lex, parse, typecheck (incremental)
mako check --json path.mko   # JSON diagnostics for CI/IDE/AI tooling
mako build path.mko -o bin   # → C → .o cache → link (debug -O0; --release -O3 -flto)
mako build -j 8 --no-incremental path.mko   # parallel jobs; disable cache
mako run path.mko [-- args...]   # compile + run; trailing args → argc/args
mako test [path] [--run PAT] [-v] [--count N] [--coverage]  # tests + categories
mako fmt [paths...] [-w|-l|-d] [-p NAME]   # formatter: stdout / write / list / diff
mako lint [path] [-p NAME]            # workspace-aware typecheck + rules
mako bench [path] [-p NAME] [--json]  # workspace-aware bench_*.mko wall time
mako profile [path] [-p NAME] [--release] [--json] -- [args...]  # compile/run profile
mako doc [path]              # API markdown + runnable examples + search index
mako metadata [path]         # JSON symbol graph + AST summary
mako api diff old new        # breaking API change detector
mako init [--backend|--workspace]    # app / API service / workspace
mako pkg init|list|fetch|lock|add|remove|audit   # path first-class; git via fetch
mako deploy docker [path] --entry main.mko --bin server --port 8080
mako deploy serverless [path] --provider cloud-run|fly --name mako-app
mako deploy wasm wasm-dist --entry examples/wasi_hello.mko --wasm hello.wasm
mako deploy plugin my-plugin --name my-plugin --kind native
```

VS Code support under `editors/vscode/` includes syntax highlighting, snippets,
tasks, command palette actions, `mako-native` debug launch configs, and a
dependency-free client for `mako lsp` covering diagnostics, hover, completion,
definitions, references, rename, code actions, symbols, and signature help.
Configure the executable path with `mako.path`; native debugging delegates to
CodeLLDB (`lldb`) or Microsoft C/C++ (`cppdbg`) via `mako.debug.adapter`.

Flags of note: `--time`, `-j` / `MAKO_JOBS`, `--no-incremental`, `--target <triple>`,
`--sanitize=thread|address`, `--static-link`, `--no-static-link`, `--emit-c`.
Linux musl targets default to static linking; glibc Linux, macOS, Windows, and
WASM stay dynamic/default unless static linking is explicitly supported and requested.
See [BUILD.md](BUILD.md) · [PERFORMANCE.md](PERFORMANCE.md) · [SECURITY.md](SECURITY.md).

`mako deploy docker` writes a multi-stage Dockerfile plus `.dockerignore`.
Default mode builds a static `x86_64-unknown-linux-musl` binary and copies it
into `scratch`; `--mode debian` uses `debian:bookworm-slim` with CA certificates
for apps that need OS trust stores or shell/debug tooling.
`mako deploy serverless` builds on that Dockerfile and writes provider starter
manifests: `cloudrun.service.yaml` for Cloud Run or `fly.toml` for Fly.io.
`mako deploy wasm` writes a browser/edge static starter around the WASI
preview1 loader: `index.html`, `mako-wasi-loader.js`, `build-wasm.sh`, and a
README that names the preview2/component boundary.
`mako deploy plugin` writes native or WASM plugin skeletons using the ABI seed
in `runtime/mako_plugin.h`; see [ABI.md](ABI.md).

### Security APIs (stdlib)

```mko
assert(const_eq(got, want) == 1)          // timing-safe
let s = secret_from_str(key); secret_drop(s)
assert(http_header_ok(name, val) == 1)    // reject CR/LF injection
unsafe { let v = unsafe_index(xs, i) }    // explicit bounds opt-out
// DB: sqlite_query_int / sqlite_query_int_params — parameterized only
// Crew exit cancel_joins; [package] systems = true forbids GC weakening
```

### Session Management, Authentication & Authorization

Mako ships a complete toolkit for cookies, sessions, CSRF protection,
authentication, signed tokens, and role-based access control. All security-
sensitive comparisons use constant-time equality to prevent timing attacks.

Runtime: `runtime/mako_security.h`.

#### Cookies

| Builtin | Signature | Purpose |
|---------|-----------|---------|
| `cookie_get` | `(header: string, name: string) -> string` | Parse a cookie value from a `Cookie:` header |
| `cookie_make` | `(name: string, value: string, max_age: int) -> string` | Create a `Set-Cookie` header string (HttpOnly, SameSite=Lax, Path=/) |

```mko
let cookie_hdr = http_header(c, "Cookie")
let sid = cookie_get(cookie_hdr, "sid")

let set_cookie = cookie_make("sid", session_id, 86400)  // 24-hour expiry
let _ = http_respond_ct(c, 200, "application/json", body, set_cookie)
```

#### Sessions

| Builtin | Signature | Purpose |
|---------|-----------|---------|
| `session_id_new` | `() -> string` | Generate a 32-char random hex session ID (16 bytes of cryptographic randomness) |
| `auth_session_cookie` | `(cookie_header: string, cookie_name: string, expected: string) -> int` | Constant-time check of a session cookie against expected value (1=match, 0=no) |

```mko
let sid = session_id_new()           // e.g. "a3f8...c210" (32 hex chars)
cmap_set(sessions, sid, user)

// Later, on a protected route:
let cookie_hdr = http_header(c, "Cookie")
if auth_session_cookie(cookie_hdr, "sid", expected_sid) == 1 {
    // session valid
}
```

#### CSRF Protection

| Builtin | Signature | Purpose |
|---------|-----------|---------|
| `csrf_token` | `() -> string` | Generate a random CSRF token |
| `csrf_check` | `(expected: string, submitted: string) -> int` | Constant-time comparison (1=match, 0=no) |

```mko
let token = csrf_token()
// embed token in the HTML form or return it as a response header

// On form submission:
let submitted = json_get_string(body, "csrf_token")
if csrf_check(token, submitted) == 0 {
    let _ = http_respond_json(c, 403, "{\"error\":\"CSRF token mismatch\"}")
    return
}
```

#### Authentication

| Builtin | Signature | Purpose |
|---------|-----------|---------|
| `auth_bearer` | `(authorization: string) -> string` | Extract token from "Bearer \<token\>" header |
| `auth_check_bearer` | `(authorization: string, expected_token: string) -> int` | Constant-time bearer token verification |
| `auth_basic_header` | `(user: string, pass: string) -> string` | Build a "Basic \<base64\>" authorization header |
| `auth_check_basic` | `(authorization: string, user: string, pass: string) -> int` | Verify Basic auth credentials |

```mko
// Bearer token
let auth_hdr = http_header(c, "Authorization")
let token = auth_bearer(auth_hdr)                       // extract raw token
if auth_check_bearer(auth_hdr, expected_token) == 1 {
    // authorized
}

// Basic auth
let hdr = auth_basic_header("admin", "s3cret")          // build header
if auth_check_basic(auth_hdr, "admin", "s3cret") == 1 {
    // credentials valid
}
```

#### Signed Tokens (HMAC-SHA256)

| Builtin | Signature | Purpose |
|---------|-----------|---------|
| `auth_token_sign` | `(subject: string, secret: string) -> string` | Sign a subject, returns "subject.hmac_signature" |
| `auth_token_check` | `(token: string, secret: string) -> int` | Verify a signed token (1=valid, 0=invalid) |
| `auth_token_subject` | `(token: string) -> string` | Extract subject from "subject.signature" token |

```mko
let secret = "my_signing_key"
let token = auth_token_sign("user:42", secret)   // "user:42.abc123..."
if auth_token_check(token, secret) == 1 {
    let sub = auth_token_subject(token)           // "user:42"
}
```

#### Role-Based Access Control

| Builtin | Signature | Purpose |
|---------|-----------|---------|
| `auth_role_has` | `(roles_csv: string, role: string) -> int` | Check if a comma-separated roles string contains a role |
| `authz_allow_role` | `(user_roles_csv: string, required_roles_csv: string) -> int` | Check if user has any of the required roles |

```mko
let user_roles = "editor,viewer"
if auth_role_has(user_roles, "admin") == 0 {
    // user is not an admin
}
if authz_allow_role(user_roles, "editor,admin") == 1 {
    // user has at least one of the required roles
}
```

#### Practical Session Flow

```mko
let sessions = cmap_new()

fn handle_login(c: int) {
    let body = http_body(c)
    let user = json_get_string(body, "user")
    let pass = json_get_string(body, "pass")

    // verify credentials (your logic here)

    let sid = session_id_new()
    cmap_set(sessions, sid, user)
    let cookie = cookie_make("sid", sid, 86400)  // 24h
    let _ = http_respond_ct(c, 200, "application/json", "{\"ok\":true}", cookie)
}

fn handle_protected(c: int) {
    let cookie_hdr = http_header(c, "Cookie")
    let sid = cookie_get(cookie_hdr, "sid")
    if cmap_has(sessions, sid) == 0 {
        let _ = http_respond_json(c, 401, "{\"error\":\"unauthorized\"}")
        return
    }
    let user = cmap_get(sessions, sid)
    let _ = http_respond_json(c, 200, json_object("user", user))
}
```

Security properties:
- All token/password comparisons use constant-time equality (prevents timing attacks)
- Cookies default to HttpOnly (no JS access), SameSite=Lax (CSRF protection), Path=/
- Session IDs use cryptographic random bytes (`mako_random_bytes`)
- Secrets can be wiped from memory with `secret_from_str` / `secret_drop`

---

## 17. Extern C

```mko
// examples/extern_c.mko — links runtime/mako_extern_demo.c
extern "C" fn mako_c_abs(n: int) -> int
extern "C" fn mako_c_add(a: int, b: int) -> int

fn main() {
    print_int(mako_c_abs(0 - 42))
    print_int(mako_c_add(20, 22))
}
```

Plugin ABI seed:

```bash
mako deploy plugin my-plugin --name my-plugin --kind native
mako deploy plugin my-wasm-plugin --name my-wasm-plugin --kind wasm
```

Native plugins use `runtime/mako_plugin.h` and export `mako_plugin_entry`.
WASM plugin skeletons emit a manifest and exported function sketch. Host-side
loading/capabilities remain roadmap work.

---

## WASM / WASI (preview1 Done)

With **wasi-sdk** installed (`WASI_SDK_PATH`):

```bash
mako build examples/wasi_hello.mko --target wasm32-wasi -o out/wasi_hello.wasm
wasmtime out/wasi_hello.wasm

mako build examples/wasi_args_env.mko --target wasm32-wasi -o out/wasi_args_env.wasm
wasmtime --env MAKO_WASI_GREET=hi out/wasi_args_env.wasm hello

mkdir -p out/wasi_fs_sandbox && echo seed > out/wasi_fs_sandbox/in.txt
mako build examples/wasi_fs.mko --target wasm32-wasi -o out/wasi_fs.wasm
wasmtime --dir=out/wasi_fs_sandbox::. out/wasi_fs.wasm
```

`wasm32-wasi` normalizes to **`wasm32-wasip1`**. Builds use wasi-sdk clang + a
minimal runtime (`-DMAKO_WASI` — print/fib/`argc`/`arg_get`/`env_get`/
`read_file`/`write_file` OK with `--dir` preopens; sockets/TLS/DB stay
native-only). Pass env from the host (`wasmtime --env`); `env_set` soft-fails
on WASI. If the SDK or wasmtime is missing, `./scripts/wasi-verify.sh` prints
`skip:` and exits 0. Details: [WASM.md](WASM.md).

For browser/edge static hosting:

```bash
mako deploy wasm wasm-dist --entry examples/wasi_hello.mko --wasm hello.wasm
./wasm-dist/build-wasm.sh
python3 -m http.server -d wasm-dist 8080
```

---

## Target / roadmap (not in this guide as “works”)

**Already Done** (see STATUS): CFG NLL, HTTP/1.1 + HTTPS + H2 TLS beachhead,
gRPC/H3-client pieces, WASI preview1, operators/imports/`mako version`,
stdlib Waves 1–9 (~98% major areas).

Still **Target / Later** (VISION): colored `async`/`await`, complete Unicode property database / full PCRE
(script seeds: Latin/Greek/Cyrillic/Arabic/Hebrew/Han/Hiragana/Katakana/Hangul/Thai/Devanagari/Tamil/Armenian/Ethiopic/Georgian/Cherokee/Bengali/Sinhala/Myanmar/Khmer/Tibetan/Syriac/Coptic/Runic/Thaana/Tagalog/Bopomofo/Braille/Ogham/Gothic/Canadian/Gujarati/Kannada/Malayalam/Telugu/Oriya/Lao/Balinese/Javanese/Sundanese/Buginese/Cham/Rejang/Lisu/Nko/Tifinagh/Samaritan/Mandaic/Saurashtra/Tai_Le/Kayah_Li/New_Tai_Lue/Ol_Chiki/Limbu/Lepcha/Batak/Tai_Tham/Syloti_Nagri/Vai/Yi/Glagolitic/Meetei_Mayek/Phags_Pa/Buhid/Hanunoo/Tagbanwa/Bamum/Mongolian/Tai_Viet/Inherited/Common + categories incl. Mn/Mc/Sm/Sk/Pc),
Huffman JPEG viewer parity (mako APP7 layout checks + roundtrip + DCT/huff evidence today; not full viewer Huffman), reflect for non-POD fields
(nested POD flatten done; map/slice/chan/nested map·slice·Option·Result fields rejected), full SMTP AUTH-over-TLS polish, native WASI preview2 / browser DOM,
optional GC, SIMD/GPU, deep LSP, homebrew-core publish (external). See [VISION.md](VISION.md)
and [STATUS.md](STATUS.md).

**Book:** [The Mako Book](book/).

---

## Example index

| Topic | File |
|-------|------|
| Hello | `examples/hello.mko` |
| WASI hello | `examples/wasi_hello.mko` |
| WASI args / env | `examples/wasi_args_env.mko` |
| WASI FS preopens | `examples/wasi_fs.mko` |
| Conversions | `examples/convert.mko`, `examples/testing/convert_test.mko` |
| make / []byte("lit") | `examples/make_bytes.mko`, `examples/testing/make_bytes_test.mko` |
| int family | `examples/integers.mko`, `examples/bytes.mko`, `examples/bad/int*` |
| Slices / copy / []byte | `examples/slice.mko`, `slice64.mko`, `bytes.mko`, `testing/slice*_test.mko`, `bytes_test.mko` |
| Types OK / bad | `examples/types_ok.mko`, `examples/bad/` |
| Result / match | `examples/result.mko`, `examples/match.mko`, `match_int.mko`, `match_or.mko` |
| Interfaces (dyn) | `examples/iface_method.mko`, `iface_self.mko`, `iface_dyn.mko`, `iface_multi.mko`, `iface_unit_dyn.mko` |
| Enum methods | `examples/enum_method.mko` |
| Arena []Struct | `examples/arena_struct.mko`, `arena_append.mko` |
| path / file / env / args / log | `examples/path_join.mko`, `file_env.mko`, `fs_polish.mko`, `cli_args.mko`, `log_ts.mko` |
| Hold partial moves | `examples/hold_partial.mko`, `hold_nested_partial.mko` |
| parse / base64 / sort / regex | `examples/parse_num.mko`, `base64.mko`, `sort.mko`, `regex_seed.mko` |
| Multi-file import | `examples/import_main.mko`, `import_as_main.mko`, `testing/import_mod_test.mko`, `import_as_test.mko`, `import_group_test.mko`, `import_brace_test.mko` |
| Hold Copy re-read | `examples/hold_copy_reread.mko` |
| exit / duration | `examples/exit_code.mko`, `testing/duration_test.mko`, `testing/exit_builtin_test.mko` |
| HTTP/1.1 server | `examples/http_server.mko` (`./scripts/http-server-smoke.sh`) |
| JSON API backend | `examples/api_backend/` (`./scripts/api-backend-smoke.sh`) |
| HTTP library | `examples/http_lib/` (`./scripts/http-lib-smoke.sh`) · `testing/http_lib_test.mko` |
| Systems append log | `examples/systems_log/` |
| Mini KV engine | `examples/db_engine/` · `testing/db_engine_test.mko` |
| Microbench | `examples/bench/` · `./scripts/bench.sh` |
| HTTPS server (OpenSSL) | `examples/https_server.mko` (`./scripts/https-server-smoke.sh`) |
| HTTP/2 TLS server | `examples/h2_server.mko` (`./scripts/h2-server-smoke.sh`) |
| HTTP client | `examples/http_get.mko` |
| Crew / channels | `examples/concurrency.mko`, `examples/channels.mko` |
| Select | `examples/select_default.mko`, `examples/select_fair.mko` |
| Actors | `examples/actor.mko` |
| HTTP / WS / TLS | `examples/http_*.mko`, `ws_*.mko`, `tls_*.mko` |
| Poll / kqueue | `examples/io_*.mko` |
| JSON | `examples/derive_json.mko`, `json_*.mko`, `json_map.mko` |
| SQLite / Redis | `examples/sqlite.mko`, `redis_ping.mko` |
| Tests | `examples/testing/` |
| Extern | `examples/extern_c.mko` |
