# How-To Guides

Practical, hands-on tutorials for building with Mako. Each guide includes
working code you can run immediately.

## Guides

| # | Guide | What you will build / learn |
|---|-------|----------------------------|
| 01 | [Getting Started](01-getting-started.md) | Install Mako, create a project, build and run your first program |
| 02 | [HTTP APIs](02-http-apis.md) | Build a JSON API server with routing, request parsing, and a client |
| 03 | [Errors and Debugging](03-errors-debugging.md) | Handle errors with Result and `?`, wrap context, debug with `dbg` and lldb |
| 04 | [Packages](04-packages.md) | Create reusable packages, manage dependencies, set up workspaces |
| 05 | [Concurrency](05-concurrency.md) | Use crew blocks, channels, select, fan, and actors for parallel work |
| 06 | [Memory](06-memory.md) | Ownership (`hold`/`share`), auto-free of slices/maps/strings, `string_view`, arenas |
| 07 | [WASI](07-wasi.md) | Compile to WebAssembly, run with wasmtime, pass args and access files |
| 08 | [Testing](08-testing.md) | Write tests, run them, filter by name, measure coverage, use subtests |
| 09 | [Release Builds](09-release-builds.md) | Optimize binaries, cross-compile, static link, package for deployment |
| 10 | [Collections](10-collections.md) | Maps, slices, nested maps, bags/channels/tuples, demand-driven monomorphs, `maps_*` |

## Prerequisites

All guides assume you have Mako installed (`mako version` → **mako0.3.0** …).
Guide 01 covers installation from scratch.

## Related documentation

- [The Mako Book](../book/) — guided language tour
- [GUIDE.md](../GUIDE.md) — syntax reference (Mako-native; §6 generics, §9 channels)
- [LANGUAGE.md](../LANGUAGE.md) — language overview + 0.2.2 generics table
- [ERGONOMICS.md](../ERGONOMICS.md) — low-ceremony maps/slices/channels
- [IDENTITY.md](../IDENTITY.md) — our syntax identity + **%**
- [COMPAT.md](../COMPAT.md) — dual forms / compatibility
- [STDLIB.md](../STDLIB.md) — standard library surface
- [BUILTINS.md](../BUILTINS.md) — current documented builtin table
- [STATUS.md](../STATUS.md) / [ROADMAP.md](../ROADMAP.md) — verified matrix / next releases
- [BUILD.md](../BUILD.md) — incremental build system
- [DEBUG.md](../DEBUG.md) — debugger integration
- [PERFORMANCE.md](../PERFORMANCE.md) · [SPEED.md](../SPEED.md) — measure & hot path
