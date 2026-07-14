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
| `for k, v in range m` | Custom iterator types |
| Annotate `map[K]V` on API boundaries | Spelling out C monomorph names |

Missing keys yield the **zero value** (empty slice, `0`/`""`/`false`, nil
inner map with `len` 0, **None** / **Err("")** for bag values) — use comma-ok
when presence matters. Nested-map `maps_clone` / `maps_equal` are shallow
(pointer identity on inners).

Full grid: [GUIDE.md §4c](GUIDE.md) · hands-on [howto/10-collections.md](howto/10-collections.md) ·
book [ch03](book/src/ch03-language-tour.md) / [cookbook](book/src/ch14-cookbook.md#collections-recipes) ·
tests under `examples/testing/map_*.mko`, `nested_slice_test.mko`, `map_option_result_test.mko`.

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
| Nested `if` pyramids for routes | `match path { … }` |
| Annotating every local | Infer locals; annotate boundaries |
| Dual spellings in new code (`func`, `:=`, `import`) | Mako flair (`fn`, `let`, `pull`) |
| Ignoring `Result` | `?`, `match`, or `let _ =` when discard is deliberate |
| Building ad-hoc set/`groupby` helpers | `map[K]bool` / `map[K][]T` |
| Re-implementing nested maps by hand | `map[K]map[K2]V` (depth 2) |
| Parallel nullable / fallible lookups | `map[K]Option[T]` / `map[K]Result[T,E]` |

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
| [PAIN_POINTS.md](PAIN_POINTS.md) | Why we exist |
| [VISION.md](VISION.md) | Product north star |
| [GUIDE.md](GUIDE.md) | Full syntax (maps §4c, slices §4b) |
| [LANGUAGE.md](LANGUAGE.md) | Surface summary |
| [BUILTINS.md](BUILTINS.md) | `maps_*` and collection builtins |
| [examples/mako_style.mko](../examples/mako_style.mko) | Canonical short sample |
| `examples/testing/map_*.mko` · `nested_slice_test.mko` | Collection surface tests |
