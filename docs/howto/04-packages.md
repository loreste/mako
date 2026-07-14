# Packages and Dependencies

This guide covers creating reusable packages, declaring dependencies, and
organizing larger projects into workspaces.

## Package basics

Every Mako project has a `mako.toml` at its root:

```toml
name = "myapp"
version = "0.1.0"
```

Create one with:

```bash
mako init myapp --name myapp
# or for a library:
mako pkg init mylib
```

## Project layout

A package with both library and application code:

```
myapp/
  mako.toml
  main.mko          # application entry point (fn main)
  lib.mko           # library code (preferred for deps)
```

When another package depends on yours, Mako includes `lib.mko` if it exists,
otherwise all top-level `.mko` files (excluding tests and main).

## Adding a local dependency

Suppose you have a helper library next to your app:

```
projects/
  helper/
    mako.toml       # name = "helper"
    lib.mko         # fn add(a: int, b: int) -> int { return a + b }
  app/
    mako.toml
    main.mko
```

In `app/mako.toml`:

```toml
name = "app"
version = "0.1.0"

[dependencies]
"helper" = { path = "../helper", version = "0.1.0" }
```

Or use the CLI:

```bash
cd app
mako pkg add helper ../helper
```

In `app/main.mko`, call functions through the dependency namespace:

```mko
fn main() {
    print_int(helper.add(2, 3))
}
```

The namespace comes from the key in `[dependencies]` -- rename it with:

```toml
"math" = { path = "../helper" }
```

Then call `math.add(2, 3)`.

## Transitive dependencies

If helper depends on core, and app depends on helper, Mako walks each
package's `mako.toml` transitively. Each package uses its own declared names:

```
app -> helper -> core
      (helper calls core.scale)
      (app calls helper.add)
```

## Git dependencies

For remote packages (requires git on PATH):

```toml
[dependencies]
"tool" = { git = "https://example.com/tool.git", tag = "v0.1.0" }
```

Then fetch:

```bash
mako pkg fetch
```

This clones into `.mako/deps/tool/`. Use `--offline` flags to prevent network
access in CI.

## Lockfile

Pin exact versions for reproducible builds:

```bash
mako pkg lock
```

This writes `mako.lock` with content hashes. Commit it to version control.

## Package commands

| Command | Purpose |
|---------|---------|
| `mako pkg init mylib` | Create a new package |
| `mako pkg add name path=../name` | Add or update a path dependency |
| `mako pkg add name ../name` | Same (positional) |
| `mako pkg remove name` | Remove a dependency |
| `mako pkg list` | Show packages and their status |
| `mako pkg fetch` | Clone git dependencies |
| `mako pkg lock` | Write/update mako.lock |
| `mako pkg audit` | Check advisories and license policy |

## Workspaces

For larger projects with multiple packages that build together:

```bash
mako init myws --workspace
```

This creates:

```
myws/
  mako.toml         # [workspace] members = ["lib", "app"]
  lib/
    mako.toml
    lib.mko
  app/
    mako.toml       # [dependencies] "lib" = { path = "../lib" }
    main.mko
```

Root `mako.toml`:

```toml
[workspace]
members = ["lib", "app"]
```

## Workspace commands

From the workspace root:

| Command | Behavior |
|---------|----------|
| `mako check .` | Typecheck all members |
| `mako build .` | Build members with `main.mko` |
| `mako test .` | Run tests in all members |
| `mako fmt .` | Format all members |
| `mako run -p app` | Run a specific member |
| `mako check -p lib` | Check a single member |

If only one member has `main.mko`, `mako run .` runs it directly.

## Security audits

Create `mako-cve.toml` beside your lockfile:

```toml
[[advisory]]
id = "CVE-2024-1234"
name = "util"
version = "<=1.2.3"
severity = "high"
```

And `mako-license.toml` for license policy:

```toml
allow = ["MIT", "Apache-2.0"]
deny = ["GPL-3.0"]

[licenses]
helper = "MIT"
```

Then run:

```bash
mako pkg audit
```

This checks offline -- no network required.

## Pulls (multi-file)

Most real projects need more than one file. Mako **pulls** are always
**pack-qualified** so call sites stay clear. `mako run` compiles everything
that’s pulled.

### Basic file pull

```mko
// utils.mko
pack utils

fn format_name(first: string, last: string) -> string {
    return first + " " + last
}
```

```mko
// main.mko
pull "./utils.mko"

fn main() {
    print(utils.format_name("Grace", "Hopper"))
}
```

```bash
mako run main.mko
# Grace Hopper
```

Default qualifier: the pulled file’s `pack` name (if not `main`), else the
path basename (`utils` from `utils.mko`).

### Pack-qualified types

Exported structs (and other named types) use the same qualifier as functions:

```mko
// eng.mko
pack eng

export struct Table {
    n: int
}

export fn table_new() -> Table {
    return Table { n: 0 }
}

export fn table_grow_pair(t: Table, by: int) -> (Table, int) {
    let mut tt = t
    tt.n = tt.n + by
    return (tt, by)
}
```

```mko
// main.mko
pull "./eng.mko"

fn use(t: eng.Table) -> int {
    return t.n
}

fn main() {
    let t: eng.Table = eng.Table { n: 0 }   // pack-qualified struct lit
    let t2, n = eng.table_grow_pair(t, 5)
    match t2 {
        eng.Table { n: rows } => print_int(rows),
    }
    print_int(use(t2))
    print_int(n)
}
```

`eng.Table` in annotations, return types, struct literals, and struct patterns
is the pack form of the rewritten name `eng__Table`. Multi-return of pack
structs works the same as local structs: `let a, b = eng.f()` and
`match eng.f() { (a, b) => … }`.

### Maps of pack structs

```mko
pull "./eng.mko"

fn main() {
    let mut m = make(map[int]eng.Table)
    m[1] = eng.Table { n: 10 }
    print_int(m[1].n)
}
```

Also `map[string]eng.Table`, `map[float]eng.Table`, and pack types as **keys**
(`map[eng.Table]int`, `map[eng.Table]Point`). Same ops as other maps: `len` /
`has` / `delete`, comma-ok, `range`, and `maps_keys` / `maps_values` /
`maps_clone` / `maps_equal` / `maps_copy` / `maps_clear`. Full map value grid
(incl. `map[K][]T`, nested `map[K]map[…]`, and bag values `map[K]Option[T]` /
`map[K]Result[T,E]`) is in [GUIDE.md](../GUIDE.md) §4c.

### Pack-qualified enums

Exported enums use the same qualifier. Variants stay short names after pull;
you may qualify them with the pack (and optionally the type):

```mko
// eng.mko
pack eng
export enum Color { Red, Green(int) }
```

```mko
pull "./eng.mko"

fn score(c: eng.Color) -> int {
    match c {
        eng.Red => 1,
        eng.Green(n) => n,
        // or: eng.Color.Red / eng.Color.Green(n)
    }
}

fn main() {
    let a = eng.Red
    let b = eng.Green(7)
    let c = eng.Color.Green(3)
    print_int(score(a) + score(b) + score(c))
}
```

### Explicit aliases

```mko
pull "./db.mko" as db
pull "./routes.mko" as routes
// dual: import db "./db.mko"

fn main() {
    db.connect()
    routes.serve(8080)
}
```

### Blank and dot

```mko
pull _ "fmt"           // load/typecheck only — no names
pull . "./helpers.mko" // merge without prefix (use sparingly)
```

### Grouped pulls

When you have several pulls, group them into one block:

```mko
pull (
    "strings"
    "net/http"
    "./db.mko" as db
    "./routes.mko" as routes
)
```

`mako fmt` rewrites separate `pull` lines into this grouped form automatically.

### Standard library

Pull standard library units by name (no `./` prefix). Always qualify:

```mko
pull "strings"

fn main() {
    print(strings.trim("  hello  "))
}
```

## Walkthrough: building a multi-file project

Here's how to go from a single file to a well-organized multi-file project,
step by step.

**Step 1: Start the project**

```bash
mako init taskapi --name taskapi
cd taskapi
```

You get `mako.toml` and `main.mko`.

**Step 2: Add a data layer**

Create `db.mko` alongside `main.mko`:

```mko
// db.mko
pack db

fn init() {
    let _ = sqlite_query_int("/tmp/tasks.db",
        "CREATE TABLE IF NOT EXISTS tasks(id INTEGER PRIMARY KEY, title TEXT, done INT)")
}

fn add_task(title: string) -> int {
    return sqlite_query_int("/tmp/tasks.db",
        "INSERT INTO tasks(title, done) VALUES ('" + title + "', 0)")
}

fn task_count() -> int {
    return sqlite_query_int("/tmp/tasks.db", "SELECT COUNT(*) FROM tasks")
}
```

**Step 3: Add route handlers**

Create `routes.mko`:

```mko
// routes.mko
pack routes

pull "./db.mko"

fn health(c: int) {
    let _ = http_respond_json(c, 200, "{\"ok\":true}")
}

fn add_task(c: int) {
    let body = http_body(c)
    let title = json_get_string(body, "title")
    let _ = db.add_task(title)
    let _ = http_respond_json(c, 201, "{\"created\":true}")
}

fn stats(c: int) {
    let count = db.task_count()
    let _ = http_respond_json(c, 200, json_ss("count", format_int(count)))
}
```

**Step 4: Wire it together in main.mko**

```mko
// main.mko
pull "./db.mko"
pull "./routes.mko"

fn main() {
    db.init()
    let fd = http_bind(8080)
    print("taskapi on :8080")

    while true {
        let c = http_accept(fd)
        let path = http_path(c)

        if str_eq(path, "/health") {
            routes.health(c)
        } else {
            if str_eq(path, "/tasks") {
                routes.add_task(c)
            } else {
                if str_eq(path, "/stats") {
                    routes.stats(c)
                } else {
                    let _ = http_respond(c, 404, "not found\n")
                }
            }
        }
        let _ = http_close(c)
    }
}
```

**Step 5: Run it**

```bash
mako run main.mko
# taskapi on :8080
```

That's it. The compiler follows the imports and compiles `db.mko` and
`routes.mko` automatically.

**Your project now looks like this:**

```
taskapi/
  mako.toml
  main.mko        # entry point, imports everything
  db.mko          # data layer
  routes.mko      # HTTP handlers
```

## Extracting a shared package

Once code is useful across multiple projects, move it into its own package.

**Step 1: Create the shared package**

```bash
mkdir -p ../shared
cd ../shared
mako pkg init shared
```

Put reusable code in `lib.mko`:

```mko
// shared/lib.mko
fn add(a: int, b: int) -> int {
    return a + b
}

fn greet(name: string) -> string {
    return "hi " + name
}
```

**Step 2: Add it as a dependency**

Back in your app:

```bash
cd ../taskapi
mako pkg add shared ../shared
```

This adds to your `mako.toml`:

```toml
[dependencies]
shared = { path = "../shared" }
```

**Step 3: Use it**

```mko
// main.mko
fn main() {
    print(shared.greet("world"))
    print_int(shared.add(1, 2))
}
```

Package functions are called through the dependency name as a namespace --
`shared.greet()`, not `greet()`.

## Next steps

- [Concurrency patterns](05-concurrency.md)
- [Release builds and deployment](09-release-builds.md)
