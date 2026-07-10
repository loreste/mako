# 14. Cookbook / How-tos

Task-oriented recipes live under [`docs/howto/`](../../howto/). Use this chapter
as an index; prefer the how-to pages for step-by-step detail.

| Guide | When you need… |
|-------|----------------|
| [01 Getting started](../../howto/01-getting-started.md) | Install, init, first run |
| [02 HTTP APIs](../../howto/02-http-apis.md) | Client/server HTTP library |
| [03 Errors & debugging](../../howto/03-errors-debugging.md) | Result, dbg, lldb, sanitizers |
| [04 Packages](../../howto/04-packages.md) | mako.toml, pkg, registry |
| [05 Concurrency](../../howto/05-concurrency.md) | crew, channels, select |
| [06 Memory](../../howto/06-memory.md) | arena, hold, share |
| [07 WASI](../../howto/07-wasi.md) | wasm32-wasi builds |
| [08 Testing](../../howto/08-testing.md) | Tests |
| [09 Release builds](../../howto/09-release-builds.md) | `--release`, cache, perf |

## Quick recipes

**Health endpoint**

```mko
let _ = http_respond_ct(c, 200, "application/json", "{\"ok\":true}\n")
```

**Wrap an error**

```mko
let w = wrap_err(r, "load config")
```

**Format the tree**

```bash
mako fmt -w .
```

**Run one test**

```bash
mako test examples/testing -r TestAdd -v
```

**Cross-check a snippet**

Put a full `fn main()` program in a temp `.mko` and run `mako check` — the
compiler is the syntax authority.

Also: [BUILD](../../BUILD.md) · [PERFORMANCE](../../PERFORMANCE.md) ·
[DEBUG](../../DEBUG.md) · [STDLIB](../../STDLIB.md).

Next: [Appendix](ch15-appendix.md).
