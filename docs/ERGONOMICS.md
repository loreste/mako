# Low ceremony — real work, less typing

**Product rule:** Mako should let you **ship real software without drowning in
keystrokes**. Safety and structure stay; the *common path* stays short.

This is not “be like Go” or “be like Rust.” It is Mako’s own deal:

| Always | Never (as preferred) |
|--------|----------------------|
| Short everyday code | Lifetime annotations on every API |
| Visible power when you need it (`hold`, `crew`, `arena`) | Hidden GC or free-fire tasks |
| Compiler catches real bugs | Ignoring errors by default |
| Unique Mako surface | Clone another language’s ceremony |

Identity: [IDENTITY.md](IDENTITY.md) · Pain map: [PAIN_POINTS.md](PAIN_POINTS.md).

---

## The short path (use these)

### Locals — infer the type

```mko
let n = 42
let mut total = 0
let name = "mako"
let parts = strings.split("a,b", ",")
```

You write types on **API boundaries** (`fn` params / returns / struct fields),
not on every local.

### Generics — monomorphized, little ceremony

```mko
fn first[T](xs: []T) -> T {
    return xs[0]
}

struct Pair[T] { a: T, b: T }

fn main() {
    let p = Pair[int] { a: 1, b: 2 }   // type args at construction
    print(first([10, 20, 30]))         // T inferred at call
}
```

Prefer `fn f[T: I](…)` bounds over ad-hoc `any`. See [GUIDE.md](GUIDE.md) §6.

### One `print` for the common cases

```mko
print("hello")
print(42)
print(true)
print(3.14)
```

Prefer `print(x)` over `print_int` / `print_float` / … for everyday code.
Specialized printers remain when you want them.

### Compare strings with `==`

```mko
if path == "/health" {
    print("ok")
}
```

Prefer `==` / `!=` over `str_eq(...)` when both sides are strings.

### Multi-return without ceremony

```mko
fn divmod(a: int, b: int) -> (int, int) {
    return (a / b, a % b)
}

fn main() {
    let q, r = divmod(17, 5)
    print(q)
    print(r)
}
```

### Errors: `?` or `match` — not a wall of noise

```mko
fn load() -> Result[int, string] {
    let n = parse_int(read_file("n.txt"))?
    return Ok(n)
}
```

Unused `Result` is illegal — you still **handle** errors; you just do not
retype `if err != nil` ten times.

### Branch with `else if` or `match` (including strings)

```mko
// else if chain
if n < 0 {
    print("neg")
} else if n == 0 {
    print("zero")
} else {
    print("pos")
}

// string match — great for routes
match path {
    "/health" => { let _ = http_respond_json(c, 200, "{\"ok\":true}") },
    "/" => { let _ = http_respond(c, 200, "hi\n") },
    _ => { let _ = http_respond(c, 404, "missing\n") },
}
```

Prefer `match` over deep nested `if` for routing and enums.

### Methods without trait essays

```mko
on Point {
    fn distance(self) -> int {
        return self.x + self.y
    }
}

fn main() {
    let p = Point { x: 3, y: 4 }
    print(p.distance())
}
```

### Concurrency without free-floating tasks

```mko
crew t {
    let job = t.kick(work())
    print(job.join())
}
```

### Iterate with `for … in range` (not hand-rolled `while`)

Prefer range forms over index soup. Two binders need the `range` keyword; maps
always require `range`.

```mko
// slices / arrays
for i, item in range arr {
    // index + value
}
for _, item in range arr {
    // values only
}
for i in range arr {
    // indices only
}

// maps
for k, v in range m {
    // …
}

// integer span 0..n-1
for i in range n {
    // …
}

// C-style when you need a custom step
for i := 0; i < n; i++ {
    // …
}
```

Tests: `for_forms_test.mko`, `range_test.mko`, map `*_test.mko` range cases.

### Format strings with `fmt_sprintf*` (not a builder wall)

For logs, JSON fragments, and config dumps, use the fmt package surface instead
of ten `builder_write` calls. Prefer `f"…{x}"` for simple assembly, or
`fmt_sprintf*` when you need format verbs (`%x`, precision).

```mko
log_info_event(
    "listener_bound",
    fmt_sprintf3(
        "frontend=%s bind=%s:%s",
        fe.name,
        fe.bind_host,
        int_to_string(fe.bind_port)
    )
)

let line = fmt_sprintf_d("retries=%d", n)
let pair = fmt_sprintf2("%s=%s", key, val)
```

| Helper | Role |
|--------|------|
| `fmt_sprintf` … `fmt_sprintf4` | string args into `%s` / `%v` / `%q` / … |
| `fmt_sprintf_d` / `fmt_sprintf_dd` | int verbs (`%d` `%x` …) |
| `fmt_sprintf_f` | float + precision |
| `fmt_sprint` / `fmt_errorf` | join / error strings |

Tests: `fmt_print_test.mko`. Details: [BUILTINS.md](BUILTINS.md) · [STDLIB.md](STDLIB.md).

### Dispatch on string / int with `match` or `switch`

Deep nested `if str_eq(key, …) { else { if … } }` is avoidable:

```mko
// config keys, routes, status tokens
match key {
    "timeout_client" => { out.timeout_client_ms = parse_duration_ms(val) },
    "timeout_server" => { out.timeout_server_ms = parse_duration_ms(val) },
    "maxconn" => { out.maxconn = to_int(val) },
    _ => {},
}

// ints
switch status {
case 200:
    // ok
case 404, 410:
    // gone-ish
default:
    // other
}
```

Tests: `ergonomics_test.mko` (string `match`), `switch_test.mko`, `match_or_test.mko`.

### Multi-field worker results — POD struct channels (no int bit-packing)

Workers should not pack five fields into one `int` with multiply/modulo.
Send a **named POD struct** on a channel (or as a kick arg):

```mko
struct ProxyDone {
    err: int
    status: int
    server_idx: int
    bytes: int
    retries: int
}

fn worker(out: chan[ProxyDone], …) -> int {
    let _ = out.send(ProxyDone {
        err: 0,
        status: 200,
        server_idx: si,
        bytes: n,
        retries: r,
    })
    return 0
}

fn main() {
    let ch = chan_open[ProxyDone](64)
    crew t {
        let j = t.kick(worker(ch, …))
        let d = ch.recv()
        let _ = j.join()
        // use d.err, d.status, d.bytes, …
    }
}
```

**Send rules (kick args):** Copy scalars, `string`, deep-POD structs (scalar/string
fields only), `Option`/`Result`/tuples of sendables, channel handles.
**Not sendable as kick args:** maps, arrays, arenas, non-POD structs.
Enum fields on structs are not deep-POD yet — use int flags or send enums on
their own when allowed; prefer `chan[Struct]` for rich results.

`chan_len` / `chan_cap` work on these channels (any `chan[T]`), not only
`chan[int]`. Tuple channels: `make(chan[(int, string)], n)`.

Tests: `chan_struct_test.mko`, `chan_len_cap_test.mko`, `kick_send_test.mko`,
POD kick waves · [SPEED.md](SPEED.md) · [howto/05-concurrency](howto/05-concurrency.md).

### Units without path soup

```mko
pack lib

// caller
pull "./lib.mko"
print(lib.add(2, 3))
```

### Maps and slices — one surface, no special APIs

Everyday collections use the same operators as scalars. No separate “hashmap”
package, no iterator ceremony, no key-type APIs:

```mko
// Set-style membership
let mut seen = make(map[string]bool)
seen["a"] = true
if has(seen, "a") { }

// Group rows by key
let mut groups = make(map[string][]int)
groups["a"] = [1, 2, 3]
print(len(groups["a"]))        // 3
print(groups["a"][0])          // 1

// Nested maps (depth 2) — build the inner map, store the pointer
let mut outer = make(map[string]map[string]int)
let mut row = make(map[string]int)
row["x"] = 1
outer["a"] = row
print(outer["a"]["x"])         // 1

// Struct / enum keys — field-wise / tag equality, no hand-rolled hash
struct Point { x: int, y: int }
enum Color { Red, Green }
let mut by_pt = make(map[Point]int)
by_pt[Point { x: 1, y: 2 }] = 10
let mut by_e = make(map[Color][]string)
by_e[Red] = ["hot"]

// Nested slices
let grid: [][]int = [[1, 2], [3]]
print(grid[0][1])              // 2

// Map of nested slices (e.g. sparse grids by name)
let mut grids = make(map[string][][]int)
grids["board"] = [[1, 0], [0, 1]]

// Optional / fallible values per key (bag values)
let mut maybe = make(map[string]Option[int])
maybe["a"] = Some(42)
maybe["b"] = None
match maybe["a"] {
    Some(v) => print(v),
    None => {},
}
let mut tried = make(map[int]Result[string, string])
tried[1] = Ok("yes")
tried[2] = Err("no")

// Bulk helpers when you need them
let ks = maps_keys(groups)
let c = maps_clone(groups)
maps_clear(c)
```

| Prefer | Avoid (extra ceremony) |
|--------|------------------------|
| `m[k] = v` / `m[k]` / `has(m, k)` | Hand-rolled hash tables |
| `let v, ok = m[k]` | Sentinel values for “missing” |
| `map[string]bool` as a set | Parallel `[]string` + linear search |
| `map[string][]T` for groups | Nested manual lists keyed by string |
| `map[K]Option[T]` / `map[K]Result[T,E]` | Parallel maps + sentinel ints |
| `map[K]Option[Result[T,E]]` for optional fallible | Nested ad-hoc status ints |
| `map[K](Option[T], U)` for value + flag | Parallel maps for related fields |
| `map[K]chan[T]` for named mailboxes | Parallel channel vars + string switch |
| `for k, v in range m` | Custom iterator types |
| Annotate `map[K]V` on API boundaries | Spelling out C monomorph names |

Missing keys yield the **zero value** (empty slice, `0`/`""`/`false`, nil
inner map with `len` 0, **None** / **Err("")** for bag values, **nil channel**
for `chan[T]` values) — use comma-ok when presence matters. Nested-map and
channel-map `maps_clone` / `maps_equal` are shallow (pointer identity on inners).

**Compile cost:** monomorph helpers are emitted only for map shapes you use
(demand-driven) — large packs do not pay for unused bag/struct grids.

Full grid: [GUIDE.md §4c](GUIDE.md) · hands-on [howto/10-collections.md](howto/10-collections.md) ·
book [ch03](book/src/ch03-language-tour.md) / [cookbook](book/src/ch14-cookbook.md#collections-recipes) ·
tests under `examples/testing/map_*.mko`, `nested_slice_test.mko`, `map_option_result_test.mko`,
`map_option_result_nested_test.mko`, `map_tuple_bag_test.mko`, `map_chan_test.mko`.

### Big import blocks (real services)

Grouped lists with blank lines, nested std paths, aliases, and module paths are
supported — so you can pull a whole stack without retyping ceremony:

```mko
import (
    "encoding/json"
    "fmt"
    "net/http"
    "strings"

    "github.com/sirupsen/logrus"

    "izi-iva/pkg/acd"
    http_server "izi-iva/pkg/http"
)
```

Same under preferred `pull ( … )`. Details: [GUIDE.md](GUIDE.md) · `examples/import_paths/`.

---

## Power is opt-in (type more only when it pays)

| When you care | Type this |
|---------------|-----------|
| Unique ownership / moves | `hold let x = …` |
| Shared reads | `share let x = …` |
| Request/batch bulk free | `arena a { … }` |
| Parallel map | `fan` |
| Crossing pack boundaries | types on `export` APIs |

Everyday `let` stays simple. That is intentional.

---

## Anti-patterns (extra typing for no gain)

| Avoid | Prefer |
|-------|--------|
| `print_int(x)` when `print(x)` works | `print(x)` |
| `str_eq(a, b)` for plain strings | `a == b` |
| Nested `if` pyramids for routes / config keys | `match path { … }` / `match key { … }` |
| `while i < len(arr) { … i = i + 1 }` | `for i, v in range arr` / `for _, v in range arr` |
| Ten `builder_write` calls for one log line | `fmt_sprintf*` / `fmt_sprint*` |
| Packing `(err, status, idx, bytes, retries)` into one `int` | `chan[ProxyDone]` POD struct (or kick POD arg) |
| Rewriting all 10 fields on every early return | `ProxyResult { err: 1, ..base }` |
| Annotating every local | Infer locals; annotate boundaries |
| Dual spellings in new code (`func`, `:=`, `import`) | Mako flair (`fn`, `let`, `pull`) |
| Ignoring `Result` | `?`, `match`, or `let _ =` when discard is deliberate |
| Building ad-hoc set/`groupby` helpers | `map[K]bool` / `map[K][]T` |
| Re-implementing nested maps by hand | `map[K]map[K2]V` (depth ≤3) |
| Parallel nullable / fallible lookups | `map[K]Option[T]` / `map[K]Result[T,E]` |
| Hand-wiring per-key channels | `map[K]chan[T]` |

### Struct update (spread) — early returns without rewriting every field

```mko
struct ProxyResult {
    err: int
    status: int
    server_idx: int
    bytes: int
    retries: int
}

fn fail(base: ProxyResult) -> ProxyResult {
    // Override only what changed; the rest comes from base.
    return ProxyResult { err: 1, status: 503, ..base }
    // also accepted: ProxyResult { ...base, err: 1 }
}
```

`S { field: v, ..base }` / `S { ...base, field: v }` — at most one `..base`.
Base must be the same struct type. Tests: `struct_update_test.mko`.

### Enum fields on POD kick / `chan[Enum]`

Unit and POD-payload enums may appear on deep-POD structs that cross `kick`,
and as channel elements:

```mko
enum ServerState { Up, Down, Draining }
struct Server { id: int, state: ServerState }

crew t {
    let j = t.kick(check(Server { id: 1, state: Up }))
    let _ = j.join()
}
let ch = chan_open[ServerState](4)
let _ = ch.send(Draining)
```

### First-class functions + capturing closures (seed)

Pass **named functions** or **lambdas** as values. Parameter type is
`fn(T, U, …) -> R`. Values are fat pointers (`MakoFn`: code + optional env).

```mko
fn apply(f: fn(int) -> int, x: int) -> int {
    return f(x)
}
fn double(n: int) -> int { return n * 2 }

// Shared respond hook (pipeline / reverse-proxy style)
fn pipeline(respond: fn(int, string) -> int) -> int {
    return respond(200, "ok")
}
fn send(code: int, body: string) -> int { return code + len(body) }

fn main() {
    print(apply(double, 21))                 // named fn value
    print(apply(|x| x + 1, 41))              // lambda
    print(pipeline(send))
    print(pipeline(|code, body| code + len(body)))
    let g: fn(int) -> int = double
    print(g(3))

    // POD capture seed (int/bool/float by value into env)
    let n = 10
    print(apply(|x| x + n, 5))               // 15
    let f: fn(int) -> int = |x| x * n
    print(f(2))
}
```

Also works with `string` params (`fn(string) -> int`, multi-arg `fn(int, string) -> int`).
**Captures:** local `int` / `bool` / `float` by value; `string` by owned clone;
**structs** by value (string fields cloned). Kick `fn` values across crew
(`MakoFn` is Send). Env is **auto-dropped** on scope exit; kick **moves** the
env into the task. Explicit `fn_drop(f)` still available. Residual: mut borrows.

```mko
let p = Pt { x: 3, y: 4 }
crew c {
    let f: fn(int) -> int = |n| n + p.x + p.y
    let j = c.kick(apply_on_worker(f, 1))
    print(j.join())  // 8
}
```

Tests: `first_class_fn_test`, `capturing_closure_test`, `struct_capture_test`,
`kick_fn_test`, `fstring_fmt_test`, `lang_ergonomics_test`.

### `f"…"` string interpolation

```mko
let s = f"frontend={fe.name} bind={host}:{port}"
// escapes: {{ and }} → literal braces

// Format specs (printf-ish seed)
f"{n:02}"      // zero-pad: 07
f"{n:+}"       // always sign: +7
f"{n: }"       // space for positive: " 7"
f"{n:#x}"      // 0xff
f"{n:#X}"      // 0XFF
f"{n:#o}"      // leading 0
f"{n:#b}"      // 0b…
f"{n:-4}"      // left-align
f"{n:04x}"     // hex with width
f"{x:+.1f}"    // float flags + precision
f"{x:.0e}"     // scientific
f"{s:4}"       // right-align width
f"{s:<4}" / f"{s:-5}"  // left-align
```

Holes accept expressions; ints/bools stringify; strings concat.
Flags: `-` `+` ` ` `#` `0` · types: `d` `x` `X` `o` `b` · float `f`/`e`/`g`.
Prefer `fmt_sprintf*` for remaining exotic printf cases.

### Struct field defaults

```mko
struct ProxyOut {
    err: int = 0
    status: int = 200
    bytes: int = 0
}
let r = ProxyOut { err: 1 }   // status=200, bytes=0 filled in
```

### Tuple channels

```mko
let ch = chan_open[(int, string)](4)
let _ = ch.send((7, "hi"))
let a, b = ch.recv()
```

Tests: `lang_ergonomics_test.mko`.

---

## Real-work shape (API sketch)

```mko
pull "strings"

fn main() {
    let fd = http_bind(8080)
    print("on :8080")
    while true {
        let c = http_accept(fd)
        let path = http_path(c)
        match path {
            "/health" => {
                let _ = http_respond_json(c, 200, "{\"ok\":true}")
            },
            _ => {
                let _ = http_respond(c, 404, "missing\n")
            },
        }
        let _ = http_close(c)
    }
}
```

Bind, accept, match, respond — no framework ceremony, no GC tax, no async
coloring. That is the target density for Mako backends.

---

## Checklist for new features

Before adding syntax or APIs:

1. Does the **happy path** get shorter or longer?
2. Is extra typing only required when safety/performance needs it?
3. Does it still look like **Mako** ([IDENTITY.md](IDENTITY.md))?
4. Does it close a [pain point](PAIN_POINTS.md) without importing Go/Rust ceremony?

If (1) is “longer” without a safety win, reject it.

---

## Related

| Doc | Role |
|-----|------|
| [IDENTITY.md](IDENTITY.md) | Unique surface |
| [PAIN_POINTS.md](PAIN_POINTS.md) | Why we exist · residuals R8–R11 (spread, HOF, interpol, enum kick) |
| [VISION.md](VISION.md) | Product north star |
| [GUIDE.md](GUIDE.md) | Full syntax (maps §4c, control flow §4, concurrency §8–9) |
| [SPEED.md](SPEED.md) | Send-like kick · fan · channels |
| [ROADMAP.md](ROADMAP.md) | What’s done vs open for language ergonomics |
| [LANGUAGE.md](LANGUAGE.md) | Surface summary |
| [BUILTINS.md](BUILTINS.md) | `maps_*`, `fmt_*`, collection builtins |
| [howto/05-concurrency.md](howto/05-concurrency.md) | Crew / channels / struct results |
| [examples/mako_style.mko](../examples/mako_style.mko) | Canonical short sample |
| `examples/testing/map_*.mko` · `for_forms_test` · `fmt_print_test` · `chan_struct_test` | Surface tests |
