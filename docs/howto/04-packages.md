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

## Imports (multi-file)

Within a single package, use `import` to pull in other files:

```mko
// main.mko
import "./utils.mko"
import "./handlers.mko" as handlers

fn main() {
    handlers.serve(8080)
}
```

Standard library packages:

```mko
import "strings"
import "path"

fn main() {
    assert(strings.contains("mako", "ma"))
}
```

Grouped imports (preferred style):

```mko
import (
    "strings"
    "path"
    lib "./mylib.mko"
)
```

`mako fmt` will sort and group your imports automatically.

## Next steps

- [Concurrency patterns](05-concurrency.md)
- [Release builds and deployment](09-release-builds.md)
