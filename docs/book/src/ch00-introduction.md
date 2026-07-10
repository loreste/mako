# Introduction

Welcome to **The Mako Book**.

Mako is a systems and backend language: clear to write, strict at compile time,
fast at runtime, and designed so builds stay fast. Sources
use the **`.mko`** extension. The compiler emits C, then links with clang into a
single native binary — no mandatory garbage collector.

This book teaches **idiomatic Mako** as it ships today (v0.1 · STATUS north-star
**100%** · stdlib ~**98%** of major standard library *areas* · suite **130**). For the
exhaustive syntax matrix, keep [GUIDE.md](../../GUIDE.md) open beside you. For
“is this Done?”, trust [STATUS.md](../../STATUS.md).

| Chapter | What you learn |
|---------|----------------|
| [1. Preface](ch01-preface.md) | Why Mako exists |
| [2. Getting Started](ch02-getting-started.md) | Install, `mako version`, hello |
| [3. Language Tour](ch03-language-tour.md) | Syntax, types, operators |
| [4. Ownership](ch04-ownership.md) | `hold` / `share` / arenas / NLL |
| [5. Errors](ch05-errors.md) | `Result`, wrapping, `?` |
| [6. Concurrency](ch06-concurrency.md) | crew, channels, actors |
| [7. Stdlib](ch07-stdlib.md) | Packages by area |
| [8. Networking](ch08-networking.md) | HTTP, TLS, Request |
| [9. Data](ch09-data.md) | JSON, SQL, files |
| [10. Packages](ch10-packages.md) | `mako.toml`, fmt, test |
| [11. Speed & safety](ch11-speed-safety.md) | Release builds, security |
| [12. Cross-platform](ch12-cross-platform.md) | Targets, WASI |
| [13. Tooling](ch13-tooling.md) | LSP, debug, version |
| [14. Cookbook](ch14-cookbook.md) | Links into howto/ |
| [15. Appendix](ch15-appendix.md) | Keywords, roadmap, status |

Checkable samples live in [`../examples/`](../examples/).
