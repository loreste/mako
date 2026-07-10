# 7. Standard Library

Mako ships a standard library focused on backend development: strings, formatting,
file I/O, networking, encoding, cryptography, synchronization, and database
clients. This chapter tours the major packages with imports and usage examples.

You can call many helpers as bare builtins (`str_split`, `path_join`, ...) or
import packages for namespaced access (`strings.split`, `path.clean`, ...).

```mko
import "strings"
import "path"

fn main() {
    let parts = strings.split("a,b,c", ",")
    print(strings.join(parts, "/"))
    print(path.clean("/x/../y"))
}
```

Bare `import "strings"` resolves from the standard library directory (`std/`,
overrideable via `MAKO_STD`). The import auto-aliases so `strings.split` works
immediately.

---

## Strings

The `strings` package provides operations on string values. Strings in Mako are
owned, heap-allocated, null-terminated byte sequences with a length field.

### Builtins (no import needed)

```mko
fn main() {
    // Length (bytes)
    print_int(len("hello"))         // 5
    print_int(rune_count("cafe\u0301"))  // Unicode code points

    // Comparison
    if str_eq("a", "a") {
        print("equal")
    }

    // Search
    if str_contains("hello world", "world") {
        print("found")
    }

    // Concatenation
    let s = "ma" + "ko"
    print(s)

    // Indexing (byte access)
    let c = "hello"[0]     // byte value
    print_int(int(c))      // 104

    // Slicing (by bytes)
    print("hello"[1:4])    // "ell"
    print("hello"[:2])     // "he"
    print("hello"[3:])     // "lo"
}
```

### Package functions

```mko
import "strings"

fn main() {
    let parts = strings.split("a:b:c", ":")
    print(strings.join(parts, ", "))       // "a, b, c"
    print(strings.trim("  hi  "))          // "hi"
    print(strings.to_upper("mako"))        // "MAKO"
    print(strings.to_lower("MAKO"))        // "mako"
    print(strings.replace("aXbXc", "X", "-"))  // "a-b-c"

    if strings.has_prefix("hello", "he") {
        print("yes")
    }
    if strings.has_suffix("file.mko", ".mko") {
        print("mako file")
    }

    print_int(strings.index("hello", "ll"))  // 2
    print_int(strings.count("banana", "a"))  // 3
}
```

### String-to-number conversions

```mko
fn main() {
    // Parse
    match parse_int("42") {
        Ok(n) => print_int(n)
        Err(e) => print(e)
    }

    // Format
    print(format_int(123))
    print(string(42))       // int to string
}
```

---

## fmt (Formatting)

Format values into strings for output or logging:

```mko
import "fmt"

fn main() {
    print(format_int(42))
    print(format_float(3.14))
    print(format_bool(true))

    // Sprintf-style (limited)
    log_info("request handled")
    log_warn("slow query")
    log_error("connection failed")
}
```

---

## bufio (Buffered I/O)

Buffered reading and writing for efficient I/O operations:

```mko
import "bufio"

fn main() {
    let content = read_file("data.txt")
    let lines = str_split(content, "\n")
    for line in lines {
        print(line)
    }
}
```

---

## os (Operating System)

File system operations, environment variables, and process interaction:

```mko
fn main() {
    // File I/O
    let _ = write_file("/tmp/test.txt", "hello mako")
    let body = read_file("/tmp/test.txt")
    print(body)     // "hello mako"

    // Environment
    let _ = env_set("APP_MODE", "production")
    let mode = env_get("APP_MODE")
    print(mode)     // "production"

    // Command-line arguments
    print_int(argc())
    if argc() > 1 {
        print(arg_get(1))
    }

    // Exit
    // exit(1)
}
```

---

## path

Path manipulation (platform-aware joining, cleaning, splitting):

```mko
import "path"

fn main() {
    let p = path_join("foo", "bar")
    print(p)                            // "foo/bar"
    print(path_clean("/a/../b/./c"))    // "/b/c"
}
```

The `filepath` package adds glob and walk:

```mko
import "filepath"

fn main() {
    // Walk directory tree
    let count = filepath_walk_n("/tmp", 100)
    print_int(count)
}
```

---

## time

Time measurement and formatting:

```mko
fn main() {
    let start = now_ms()
    sleep_ms(50)
    let elapsed = now_ms() - start
    print_int(elapsed)              // ~50

    let formatted = time_format(now_ms())
    print(formatted)                // human-readable timestamp
}
```

---

## math

Numeric operations and constants:

```mko
fn main() {
    print_int(abs(-42))         // 42
    print_int(min(3, 7))        // 3
    print_int(max(3, 7))        // 7

    // Float math
    print_float(sqrt(16.0))     // 4
    print_float(pow(2.0, 10.0)) // 1024
}
```

---

## encoding/json

JSON encoding and decoding. See Chapter 9 for the full JSON API. Quick overview:

```mko
fn main() {
    // Build JSON objects
    let obj = json_ss("name", "Ada", "city", "London")
    print(obj)      // {"name":"Ada","city":"London"}

    // Extract fields
    let name = json_get_string(obj, "name")
    print(name)     // Ada

    // From map
    let mut m = make(map[string]string, 4)
    m["key"] = "value"
    let j = json_object_from_map_ss(m)
    print(j)
}
```

---

## encoding/base64

```mko
fn main() {
    let encoded = base64_encode("hello mako")
    print(encoded)                      // aGVsbG8gbWFrbw==

    let decoded = base64_decode(encoded)
    print(decoded)                      // hello mako
}
```

---

## encoding/hex

```mko
fn main() {
    let h = hex_encode("ok")
    print(h)                // 6f6b

    let d = hex_decode(h)
    print(d)                // ok
}
```

---

## encoding/csv

```mko
fn main() {
    let row = csv_escape("hello, world")
    print(row)      // "hello, world" (quoted because of comma)
}
```

---

## encoding/xml

```mko
fn main() {
    let safe = xml_escape("<tag attr=\"val\">")
    print(safe)     // &lt;tag attr=&quot;val&quot;&gt;
}
```

---

## crypto

Cryptographic hashing and AEAD encryption (when OpenSSL is linked):

```mko
fn main() {
    let h = sha256_hex("hello")
    print(h)    // 64-char hex digest

    let h2 = md5_hex("test")
    print(h2)   // 32-char hex digest
}
```

---

## compress/gzip

```mko
fn main() {
    let compressed = gzip_compress("hello world hello world")
    print_int(len(compressed))

    let original = gzip_decompress(compressed)
    print(original)
}
```

---

## regexp (Regular Expressions)

Pattern matching with capture groups:

```mko
fn main() {
    // Simple match check
    if regex_match("[0-9]+", "abc123def") {
        print("has numbers")
    }

    // Capture groups
    let version = regex_capture("([0-9]+)-([0-9]+)", "v1-42", 1)
    print(version)      // "1"

    let patch = regex_capture("([0-9]+)-([0-9]+)", "v1-42", 2)
    print(patch)        // "42"

    // Full match (group 0)
    let full = regex_capture("([0-9]+)-([0-9]+)", "v1-42", 0)
    print(full)         // "1-42"

    // Named-style capture
    let host = regex_capture("https?://([^/]+)", "https://example.com/path", 1)
    print(host)         // "example.com"
}
```

---

## collections: Slices

Dynamic arrays (slices) with append, index, and iteration:

```mko
fn main() {
    // Integer slices
    let xs = [1, 2, 3, 4, 5]
    print_int(len(xs))      // 5
    print_int(xs[0])        // 1

    // Append
    let ys = append(xs, 6)
    print_int(len(ys))      // 6

    // Slice expression
    let sub = xs[1:3]       // [2, 3]
    for v in sub {
        print_int(v)
    }

    // Make with capacity
    let zs = make([]int, 0, 10)     // len=0, cap=10

    // Sort
    let sorted = sort_ints([3, 1, 4, 1, 5])
    for v in sorted {
        print_int(v)
    }
}
```

---

## collections: Maps

Hash maps with string or integer keys:

```mko
fn main() {
    // String-to-int map
    let mut m = make(map[string]int)
    m["x"] = 10
    m["y"] = 20
    print_int(m["x"])       // 10
    print_int(len(m))       // 2

    // Check existence
    if has(m, "x") {
        print("has x")
    }

    // Delete
    delete(m, "x")
    print_int(len(m))       // 1

    // Iterate
    for k, v in range m {
        print(k)
        print_int(v)
    }

    // String-to-string map
    let mut ms = make(map[string]string)
    ms["name"] = "mako"
    print(ms["name"])

    // Int-to-int map
    let mut mi = make(map[int]int)
    mi[1] = 100
    mi[2] = 200
    print_int(mi[1])
}
```

---

## slices (Utility Functions)

```mko
fn main() {
    let xs = [5, 2, 8, 1, 9]
    let sorted = sort_ints(xs)
    print_int(sorted[0])    // 1

    let ss = sort_strings(["c", "a", "b"])
    print(ss[0])            // "a"
    print(ss[1])            // "b"
    print(ss[2])            // "c"
}
```

---

## context

Context provides deadline and cancellation propagation for operations:

```mko
import "context"

fn main() {
    // Timeouts for operations use crew cancel or sleep_ms-based patterns
    let start = now_ms()
    // ... operation with deadline checking ...
    let elapsed = now_ms() - start
    if elapsed > 1000 {
        print("deadline exceeded")
    }
}
```

---

## sync

Synchronization primitives for shared state:

```mko
import "sync"

fn main() {
    let m = sync.rwmutex()
    // Use with crew tasks to protect shared data
    print("mutex created")
}
```

Atomic operations are available for lock-free counters and flags.

### CMap (Concurrent Hashmap)

`CMap` is a built-in concurrent hashmap optimized for high-throughput workloads.
It uses lock-free reads and per-stripe spinlock writes (512 stripes, FNV-1a
hash, 1M initial capacity). No import needed -- it is a builtin type.

```mko
fn main() {
    let m = cmap_new()
    cmap_set(m, "greeting", "hello")
    print(cmap_get(m, "greeting"))    // "hello"
    print_int(cmap_has(m, "greeting")) // 1
    print_int(cmap_len(m))            // 1

    // Atomic counter
    let n = cmap_incr(m, "hits", 1)
    print_int(n)                      // 1
    let n2 = cmap_incr(m, "hits", 4)
    print_int(n2)                     // 5

    // Delete
    print_int(cmap_del(m, "greeting")) // 1
    print_int(cmap_len(m))             // 1
}
```

| Builtin | Purpose |
|---------|---------|
| `cmap_new()` | Create new concurrent map |
| `cmap_set(m, key, value)` | Set key-value pair |
| `cmap_get(m, key)` | Get value (`""` if missing) |
| `cmap_has(m, key)` | Key exists (1/0) |
| `cmap_del(m, key)` | Delete (returns 1 if existed) |
| `cmap_len(m)` | Entry count |
| `cmap_incr(m, key, delta)` | Atomic increment, returns new value |

Safe to share across crew tasks without mutexes or channels. Runtime:
`runtime/mako_cmap.h`.

---

## reflect

Runtime type inspection (limited to struct field names and types):

```mko
import "reflect"

fn main() {
    // Reflect on struct types at runtime
    // Primarily used by derive macros and serialization
}
```

---

## Grouped Imports

When importing multiple packages, use grouped import syntax:

```mko
import (
    "strings"
    "path"
    "sync"
    "fmt"
)

fn main() {
    let s = strings.trim("  hi  ")
    let p = path.clean("/a/../b")
    print(s)
    print(p)
}
```

The formatter (`mako fmt`) automatically rewrites two or more single imports into
a grouped block.

---

## Package Map (Reference)

| Area | Packages |
|------|----------|
| Text | `strings`, `bytes`, `strconv`, `fmt`, `unicode/utf8`, `regexp` |
| Files | `io`, `fs`, `path`, `filepath`, `bufio`, `os`, `os/exec` |
| Net | `net`, `http`, `net/url`, `net/mail`, `net/smtp` |
| Encoding | `json`, `encoding/*`, `base64`, `csv`, `binary` |
| Compress | `compress/gzip`, `archive/tar`, `archive/zip` |
| Crypto | `crypto` (hashes, AEAD when OpenSSL linked) |
| Sync | `sync`, `sync/atomic`, `context` |
| Data | `sql`, SQLite/Redis/Postgres clients |
| Other | `flag`, `log`/`slog`, `html`/`text/template`, `maps`, `slices`, `reflect` |

---

## Idioms and Best Practices

1. **Prefer package imports** for readable call sites: `strings.trim(s)` is
   clearer than `str_trim(s)` at the cost of one import line.

2. **Pre-size maps and slices** when you know the element count:
   ```mko
   let mut m = make(map[string]int, 1000)
   let xs = make([]int, 0, 256)
   ```

3. **Use arenas** for request-scoped buffers that all free together (Chapter 4).

4. **Handle results at boundaries**: parsing, I/O, and conversions return
   `Result` types. Match them explicitly.

5. **Method name aliases**: where a standard method name collides with a keyword,
   Mako uses an alias. For example `concat` instead of reserved words,
   `matches` instead of `match`.

---

## Complete Example

```mko
import (
    "strings"
    "path"
)

fn process_config(raw: string) -> string {
    let lines = strings.split(raw, "\n")
    let mut result = ""
    for line in lines {
        let trimmed = strings.trim(line)
        if len(trimmed) > 0 {
            if strings.has_prefix(trimmed, "#") {
                // skip comments
            } else {
                result = result + trimmed + "\n"
            }
        }
    }
    return result
}

fn main() {
    let config = "# Mako config\nport = 8080\n  host = 0.0.0.0\n# end\n"
    let cleaned = process_config(config)
    print(cleaned)

    let base = path_join("config", "app")
    let full = path_clean(base + "/../secrets/../app/run")
    print(full)

    let encoded = base64_encode(cleaned)
    print(encoded)

    let h = sha256_hex(cleaned)
    print(h)
}
```

Next: [Networking & HTTP](ch08-networking.md).
