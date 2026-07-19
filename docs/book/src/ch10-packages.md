# 10. Packages, Workspaces, and Tooling

Mako uses a file-based package system. The preferred module primitives are
`pack`, `pull`, and `export`:

- **`pack mylib`** — declares the current file's package identity.
- **`pull "path"`** — imports another pack (local file, relative path, or std).
- **`export fn` / `export struct`** — marks items as public to consumers.

A `mako.toml` manifest coordinates multi-file projects and external
dependencies. The `mako pkg` command manages the full lifecycle: initializing,
adding, fetching, locking, and auditing packages.

---

## pack / pull / export — The Module System

Every `.mko` file may declare its pack name at the top. Files that share the
same `pack` name belong to the same logical unit:

```mko
// mathutil.mko
pack mathutil

export fn add(a: int, b: int) -> int {
    return a + b
}

export fn mul(a: int, b: int) -> int {
    return a * b
}

// not exported — internal
fn clamp(n: int, lo: int, hi: int) -> int {
    if n < lo { return lo }
    if n > hi { return hi }
    return n
}
```

A consumer pulls the pack and accesses exported symbols through the pack name:

```mko
// main.mko
pack main

pull "./mathutil.mko"

fn main() {
    print_int(mathutil.add(2, 3))   // 5
    print_int(mathutil.mul(4, 5))   // 20
    // mathutil.clamp(...)          // compile error — not exported
}
```

### Pull forms

```mko
// Standard library — resolved from std/
pull "strings"

// Local file (pack name becomes the qualifier)
pull "./helpers.mko"

// Explicit alias
pull "./helpers.mko" as h

// Grouped pulls
pull (
    "strings"
    "./db.mko"
    "./routes.mko" as r
)
```

Bare path names like `"strings"` resolve under `std/`.
`MAKO_STD` overrides the standard library root.

### Visibility rules

- Items without `export` are private to their pack.
- `export` works on `fn`, `struct`, `enum`, and `const`.
- Within the same pack (multiple files sharing a `pack` name), all items are
  visible to each other regardless of `export`.
- Consumers only see `export`ed items.
- **Types are pack-qualified** at the consumer: `eng.Table` in annotations,
  return types, struct literals, and struct patterns (same alias as
  `eng.table_new()`). Multi-return of pack structs works: `let t, n = eng.f()`.
- **Enums** may use pack paths: `eng.Red`, `eng.Green(n)`, or
  `eng.Color.Red` / `eng.Color.Green(n)`.

### Relationship to import

The older `import` keyword still works and is equivalent to `pull` in most
contexts. New code should prefer `pack`/`pull`/`export` for clarity.

---

## mako.toml Format

Every Mako package has a `mako.toml` at its root:

```toml
[package]
name = "myapp"
version = "0.1.0"

[dependencies]
"helper" = { path = "../helper", version = "0.1.0" }
"logger" = { git = "https://github.com/org/mako-logger.git", version = "0.2.0" }
```

### Package section

| Field | Required | Description |
|-------|----------|-------------|
| `name` | Yes | Package name (used for import namespacing) |
| `version` | Yes | SemVer version string (e.g., "0.1.0") |

### Dependencies section

Each dependency is keyed by its import name and specifies a source:

```toml
[dependencies]
"dep_name" = { path = "../relative/path", version = "0.1.0" }
"dep_name" = { git = "https://...", version = "1.0.0" }
"dep_name" = { git = "https://...", branch = "main" }
"dep_name" = { git = "https://...", tag = "v1.2.3" }
```

| Source | Description |
|--------|-------------|
| `path` | Local filesystem path (relative to the manifest) |
| `git` | Git repository URL (cloned to `.mako/deps/`) |

Additional fields:
- `version` — SemVer constraint for resolution
- `branch` — Git branch to track (default: main)
- `tag` — Specific git tag to pin

---

## How Imports Work

Once a dependency is declared in `mako.toml`, its symbols are available under the
dependency key as a namespace:

```toml
# mako.toml
[dependencies]
"helper" = { path = "../helper", version = "0.1.0" }
```

```mko
// main.mko — symbols accessed via helper.fn_name()
fn main() {
    print_int(helper.add(20, 22))
    print(helper.greet("world"))
}
```

```mko
// helper/lib.mko — exports functions directly
fn add(a: int, b: int) -> int {
    return a + b
}

fn greet(name: string) -> string {
    return "hi " + name
}
```

Path dependencies are merged at compile time. The compiler resolves transitive
dependencies automatically.

---

## File-Level Imports

For single-file imports within the same package (no `mako.toml` needed), use
`pull` (or the older `import` keyword):

```mko
pull "./helpers.mko"

fn main() {
    print_int(helpers.add(2, 3))
    print(helpers.greet("mako"))
}
```

The pulled file's exported functions become available through the pack name as a
qualifier. If the file declares `pack helpers`, that name is used; otherwise the
filename basename is the default qualifier.

When you run `mako run main.mko`, the compiler automatically finds and compiles
all imported files -- you don't need to list them on the command line.

### Aliased imports

Give an import a namespace with `as`. This is the cleanest way to avoid naming
conflicts when pulling in multiple files:

```mko
import "./db.mko" as db
import "./routes.mko" as routes

fn main() {
    db.init()
    routes.serve(8080)
}
```

With an alias, you call functions through the alias name (`db.init()`) instead
of relying on prefixed function names. This works for both local file imports
and standard library imports.

### Standard library imports

```mko
import "strings"
import "path"
import "net/http"
import "sync"
```

These resolve from the standard library directory (`std/`, overrideable via the
`MAKO_STD` environment variable). Standard library modules are accessed through
their module name as a namespace:

```mko
import "strings"

fn main() {
    print(strings.trim("  hello  "))
    print(strings.contains("mako lang", "mako"))
}
```

### Grouped imports

When you have multiple imports, use the grouped syntax. This works for both
standard library and local file imports:

```mko
import (
    "strings"
    "path"
    "./db.mko"
    "./routes.mko"
)

fn main() {
    print(strings.trim("  hi  "))
    print(path.clean("/a/../b"))
}
```

The formatter (`mako fmt`) automatically rewrites two or more single `import`
statements into a grouped block.

---

## mako pkg Commands

The `mako pkg` subcommand manages the dependency lifecycle:

### mako pkg init

Initialize a new package with a `mako.toml`:

```bash
mako pkg init mylib
```

Creates:
```
mylib/
  mako.toml
  main.mko
```

The generated `mako.toml`:
```toml
[package]
name = "mylib"
version = "0.1.0"

[dependencies]
```

### mako pkg add

Add a dependency to the current package:

```bash
# Add a local path dependency
mako pkg add helper ../helper

# Add a git dependency
mako pkg add logger https://github.com/org/mako-logger.git

# Add with specific version
mako pkg add utils ../utils --version 0.2.0
```

This appends to `[dependencies]` in `mako.toml`.

### mako pkg remove

Remove a dependency:

```bash
mako pkg remove helper
```

Removes the entry from `[dependencies]` and cleans cached artifacts.

### mako pkg fetch

Download git dependencies to the local cache:

```bash
mako pkg fetch
```

Git dependencies are cloned into `.mako/deps/<name>/`. This command requires
network access. Path dependencies do not need fetching.

### mako pkg lock

Generate or update the lockfile (`mako.lock`):

```bash
mako pkg lock
```

Lockfile version 2 pins exact versions and deterministic SHA-256 hashes of each
dependency's manifest and recursive `.mko` sources:

```toml
# mako.lock (auto-generated, do not edit)
version = 2

[[package]]
name = "helper"
version = "0.1.0"
source = "path"
path = "../helper"
content_hash = "sha256:99f6a808da075bdcda271838491b9383c1e20a3be7fdb724770e4f4f18a42d14"

[[package]]
name = "logger"
version = "0.2.0"
source = "git"
path = ".mako/deps/logger"
git = "https://github.com/org/mako-logger.git"
rev = "abc1234"
content_hash = "sha256:2a78b67c8f640e71c4ef635bdf6f28c59ad31977f4041e1d7b5c3a01d5d78f2e"
```

Commit `mako.lock` to version control for reproducible builds across
environments. `mako pkg install`, `mako build`, `mako run`, and `mako check`
verify locked dependency content and fail on a mismatch, an unreadable
transitive manifest, or malformed lock metadata. Inspect intentional changes
before accepting them with `mako pkg update`; that command also migrates legacy
version 1 lockfiles.

### mako pkg update

Update dependencies to their latest compatible versions:

```bash
# Update all
mako pkg update

# Update specific dependency
mako pkg update logger
```

Re-resolves versions within SemVer constraints, recomputes integrity hashes,
and updates `mako.lock`.

### mako pkg list

List all dependencies (direct and transitive):

```bash
mako pkg list
```

Output:
```
helper  0.1.0  (path: ../helper)
logger  0.2.0  (git: https://github.com/org/mako-logger.git @ abc1234)
```

### mako pkg audit

Run security and license audits on locked dependencies:

```bash
mako pkg audit
```

Auditing checks:
- **CVE advisories**: matches against `mako-cve.toml` advisory database
- **License policy**: checks against `mako-license.toml` allow/deny rules
- **Integrity**: verifies git checksums match lockfile

```bash
$ mako pkg audit
[ok] helper 0.1.0 — no advisories
[ok] logger 0.2.0 — license: MIT (allowed)
audit: 2 packages checked, 0 issues
```

---

## Workspaces

A workspace groups multiple related packages under a single root. This is useful
for monorepo-style projects with shared libraries.

### Workspace mako.toml

```toml
[workspace]
members = ["core", "helper", "app"]
```

The workspace root has no `[package]` section — it only declares members.

### Directory structure

```
my-project/
  mako.toml          # workspace root
  core/
    lib.mko
  helper/
    lib.mko
  app/
    main.mko
```

### Transitive dependencies in workspaces

Workspace members can depend on each other. The compiler resolves these
transitively:

```mko
// core/lib.mko
fn scale(n: int) -> int {
    return n * 2
}
```

```mko
// helper/lib.mko — depends on core
fn add(a: int, b: int) -> int {
    return core.scale(a) + b
}

fn greet(name: string) -> string {
    return "hi " + name
}
```

```mko
// app/main.mko — depends on helper (which depends on core)
fn main() {
    print_int(helper.add(20, 22))   // core.scale(20) + 22 = 62
    print(helper.greet("path-dep"))
}
```

### Workspace commands

```bash
# Check all members
mako check .

# Run a specific member
mako run -p app

# Test all members
mako test .

# Format all members
mako fmt -w .
```

---

## Multi-File Projects

### Project layout conventions

```
my-service/
  mako.toml
  main.mko
  handlers/
    users.mko
    health.mko
  models/
    user.mko
  db/
    queries.mko
```

### Importing local files

```mko
import "./handlers/users.mko"
import "./handlers/health.mko"
import "./db/queries.mko"

fn main() {
    let fd = http_bind(18100)
    // ...
}
```

### Namespace conventions

Since file imports bring symbols into the same scope, use prefixes to avoid
collisions:

```mko
// handlers/users.mko
fn users_list(c: int) {
    let _ = http_respond_json(c, 200, "[]")
}

fn users_create(c: int) {
    let body = http_body(c)
    let _ = http_respond_json(c, 201, body)
}
```

```mko
// handlers/health.mko
fn health_check(c: int) {
    let _ = http_respond_json(c, 200, "{\"ok\":true}")
}
```

---

## mako fmt (Formatter)

The built-in formatter enforces consistent style:

```bash
# Print formatted output to stdout
mako fmt path.mko

# Write formatted files in place
mako fmt -w .

# List files that need formatting
mako fmt -l .

# Show diff of what would change
mako fmt -d .
```

### What the formatter does

- Normalizes indentation (4 spaces)
- Groups multiple `import` statements into `import ( ... )` blocks
- Aligns struct fields
- Normalizes whitespace around operators
- Trims trailing whitespace

### Import rewriting

Before formatting:
```mko
import "strings"
import "path"
import "sync"
```

After `mako fmt`:
```mko
import (
    "strings"
    "path"
    "sync"
)
```

---

## mako test (Testing)

Run tests with the built-in test runner:

```bash
# Run all tests in a directory
mako test examples/testing

# Filter by test name
mako test examples/testing -r TestAdd -v

# Run tests multiple times
mako test examples/testing --count 3
```

### Writing tests

Test functions are named with the `Test` prefix:

```mko
fn TestAdd() {
    assert_eq(1 + 1, 2)
}

fn TestStringConcat() {
    let s = "hello" + " " + "world"
    assert_eq(s, "hello world")
}

fn TestMapOperations() {
    let mut m = make(map[string]int)
    m["x"] = 42
    assert_eq(m["x"], 42)
    assert_eq(len(m), 1)

    // Bag values and groups — see howto/10-collections
    let mut maybe = make(map[string]Option[int])
    maybe["a"] = Some(1)
    match maybe["a"] {
        Some(v) => assert_eq(v, 1),
        None => assert(false),
    }
}
```

### Test assertions

| Function | Purpose |
|----------|---------|
| `assert_eq(a, b)` | Assert equality (integers, strings) |
| `assert(cond)` | Assert boolean condition is true |

### Optional live tests

Some tests require external services (databases, TLS). These are gated behind
environment variables:

```bash
# Enable TLS tests (requires OpenSSL)
MAKO_LIVE_TLS=1 mako test examples/testing

# Enable HTTP/2 tests
MAKO_LIVE_NGHTTP2=1 mako test examples/testing

# Enable QUIC tests
MAKO_LIVE_QUIC=1 mako test examples/testing
```

The default test suite runs green without any live services.

---

## Registry Resolution

For published packages, Mako resolves from the local registry cache:

```
.mako/registry/<key>/<version>/
```

Simple names are used as-is. Scoped names are stored in one directory segment,
so `scope/util` uses the key `scope!util` and cannot overlap another package's
version directory.

Each version contains `PACKAGE.sha256`. Mako stages this digest with the package
and verifies it before resolving or caching that version. Missing, malformed,
or mismatched digests stop resolution. New publications use a `v2:` record;
legacy unversioned records keep their original hashing rules. Package sources
may not define a top-level `PACKAGE.sha256`, which is reserved for this metadata.

SemVer resolution follows standard rules:
- `"0.1.0"` — exact version
- `"^0.1"` — compatible with 0.1.x
- `"~0.1.2"` — approximately 0.1.2 (patch-level changes)

The registry format stores package metadata and source tarballs locally after
`mako pkg fetch`.

---

## Build and Run

```bash
# Build to binary
mako build main.mko -o out/myapp

# Run directly (build + execute)
mako run main.mko

# Run with arguments
mako run main.mko -- --port 8080

# Check without building (type checking only)
mako check main.mko
```

---

## Environment Variables

| Variable | Purpose |
|----------|---------|
| `MAKO_STD` | Override standard library path |
| `MAKO_LIVE_TLS` | Enable TLS tests |
| `MAKO_LIVE_NGHTTP2` | Enable HTTP/2 tests |
| `MAKO_LIVE_QUIC` | Enable QUIC tests |

---

## Complete Project Example

A realistic project structure:

```
my-api/
  mako.toml
  main.mko
  lib/
    router.mko
    db.mko
    models.mko
```

**mako.toml:**
```toml
[package]
name = "my-api"
version = "0.1.0"

[dependencies]
```

**main.mko:**
```mko
import "./lib/router.mko"
import "./lib/db.mko"

fn main() {
    db_init()
    let fd = http_bind(18100)
    if fd < 0 {
        print("bind failed")
        return
    }
    print("my-api on :18100")

    let mut n = 0
    while n < 1000 {
        let c = http_accept(fd)
        if c >= 0 {
            router_dispatch(c)
            let _ = http_close(c)
            n = n + 1
        }
    }
    let _ = http_close_listener(fd)
}
```

**lib/router.mko:**
```mko
fn router_dispatch(c: int) {
    let method = http_method(c)
    let path = http_path(c)

    if str_eq(path, "/health") {
        let _ = http_respond_json(c, 200, "{\"ok\":true}")
    } else {
        if str_eq(path, "/api/items") {
            if str_eq(method, "GET") {
                router_items_list(c)
            } else {
                if str_eq(method, "POST") {
                    router_items_create(c)
                } else {
                    let _ = http_respond(c, 405, "method not allowed\n")
                }
            }
        } else {
            let _ = http_respond(c, 404, "not found\n")
        }
    }
}

fn router_items_list(c: int) {
    let count = db_item_count()
    let resp = json_ss("count", format_int(count))
    let _ = http_respond_json(c, 200, resp)
}

fn router_items_create(c: int) {
    let body = http_body(c)
    let name = json_get_string(body, "name")
    if len(name) == 0 {
        let _ = http_respond_json(c, 400, "{\"error\":\"name required\"}")
    } else {
        let _ = db_add_item(name)
        let _ = http_respond_json(c, 201, json_ss("name", name, "status", "created"))
    }
}
```

**lib/db.mko:**
```mko
fn db_init() {
    let _ = sqlite_query_int("/tmp/myapi.sqlite", "CREATE TABLE IF NOT EXISTS items(id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT)")
}

fn db_item_count() -> int {
    return sqlite_query_int("/tmp/myapi.sqlite", "SELECT COUNT(*) FROM items")
}

fn db_add_item(name: string) -> int {
    return sqlite_query_int("/tmp/myapi.sqlite", "INSERT INTO items(name) VALUES ('" + name + "')")
}
```

---

## Summary

| Command | Purpose |
|---------|---------|
| `mako pkg init <name>` | Create new package |
| `mako pkg add <name> <source>` | Add dependency |
| `mako pkg remove <name>` | Remove dependency |
| `mako pkg fetch` | Download git dependencies |
| `mako pkg lock` | Generate/update lockfile |
| `mako pkg update` | Update to latest compatible versions |
| `mako pkg list` | List all dependencies |
| `mako pkg audit` | Security and license audit |
| `mako fmt -w .` | Format all files |
| `mako test <dir>` | Run tests |
| `mako build <file> -o <out>` | Compile to binary |
| `mako run <file>` | Build and run |
| `mako check <file>` | Type-check only |

Next: [Speed & memory safety](ch11-speed-safety.md).
