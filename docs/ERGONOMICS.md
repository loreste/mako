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
| [GUIDE.md](GUIDE.md) | Full syntax |
| [examples/mako_style.mko](../examples/mako_style.mko) | Canonical short sample |
