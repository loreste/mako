# Mako documentation

Start with [The Mako Book](book/) for a guided introduction, or jump to what
you need below.

## Learn the language

| Doc | What's in it |
|-----|-------------|
| [Book](book/) | Guided walkthrough from zero to a working program |
| [Language Guide](GUIDE.md) | Syntax reference — types, control flow, ownership, concurrency |
| [Examples](EXAMPLES.md) | Runnable programs you can copy and modify |
| [Ergonomics](ERGONOMICS.md) | Quality-of-life features (f-strings, struct update, loop forms) |
| [Keywords](KEYWORDS.md) | All reserved words |
| [Identity](IDENTITY.md) | Why Mako looks the way it does |

## Build and run

| Doc | What's in it |
|-----|-------------|
| [CLI Reference](CLI.md) | Every command and flag |
| [Build](BUILD.md) | Backend selection, modes, cross-compilation |
| [Release](RELEASE.md) | Installing, packaging, and distributing Mako |
| [Versioning](VERSIONING.md) | How version numbers work |

## Standard library and builtins

| Doc | What's in it |
|-----|-------------|
| [Standard Library](STDLIB.md) | What's included, what's tested, what's shape-only |
| [Builtins](BUILTINS.md) | Built-in function signatures |

## Memory and safety

| Doc | What's in it |
|-----|-------------|
| [Security](SECURITY.md) | Safety model and threat boundaries |
| [Soundness](SOUNDNESS.md) | The SAFE/RT program — what's proven, what's not |
| [Memory Model](MEMORY_MODEL.md) | Concurrency rules, crew lifecycle, channel ownership |
| [Memory Safety](MEMORY_SAFETY.md) | How ownership replaces garbage collection |
| [Security Audit](SECURITY_AUDIT_SCOPE.md) | White-hat audit scope and findings |

## Performance

| Doc | What's in it |
|-----|-------------|
| [Performance](PERFORMANCE.md) | Benchmarks, including where Mako is slower |
| [Speed](SPEED.md) | How the compiler and runtime are structured for speed |

## Debugging and testing

| Doc | What's in it |
|-----|-------------|
| [Debugging](DEBUG.md) | dbg(), lldb, sanitizers, error messages |
| [TLS Live Tests](TLS_LIVE.md) | Running TLS tests that need real certificates |

## Project status

| Doc | What's in it |
|-----|-------------|
| [Status](STATUS.md) | What works and what doesn't — the honest list |
| [Roadmap](ROADMAP.md) | What's next |
| [Claims](CLAIMS.md) | What we claim and the evidence for each |
| [Pain Points](PAIN_POINTS.md) | Known rough edges |

## Architecture and internals

| Doc | What's in it |
|-----|-------------|
| [Native Compiler Plan](NATIVE_COMPILER_PLAN.md) | How the Cranelift/LLVM backend works |
| [ABI](ABI.md) | Plugin and FFI calling conventions |
| [Long Running](LONG_RUNNING.md) | Designing services that stay up for months |
| [Adaptive Opt](ADAPTIVE_OPT.md) | Profile-guided optimization (Anneal) |
| [WASM](WASM.md) | WebAssembly target status |
| [Self-Hosting](SELF_HOSTING.md) | Bring-up notes for the Mako-written compiler |
| [Self-Hosting Progress](SELF_HOSTING_PROGRESS.md) | Evidence log (not a completion claim) |
| [Self-Hosting Mandate](FULL_SELF_HOSTING_MANDATE.md) | Definition of done for full bootstrap |
| [Self-Hosting Roadmap](SELF_HOSTING_ROADMAP.md) | Phased capability matrix and migration steps |
| [Self-Hosting Execution Plan](FULL_SELF_HOSTING_EXECUTION_PLAN.md) | Milestone plan and dependency removal order |

## Design decisions

| Doc | What's in it |
|-----|-------------|
| [Vision](VISION.md) | Where Mako is headed long-term |
| [Async](ASYNC.md) | Why there's no async/await (and what we do instead) |
| [Compat](COMPAT.md) | Dual-form syntax and what won't break |
| [Go Syntax Checklist](GO_SYNTAX_CHECKLIST.md) | Go-compatible forms (accepted but not preferred) |

## Historical

These documents tracked specific planning efforts. They're kept for context
but may reference old version numbers.

| Doc | What it was |
|-----|------------|
| [General Purpose Plan](GENERAL_PURPOSE_PLAN.md) | Early backend language positioning |
| [Speed + Safety](SPEED_SAFE.md) | Combined speed/safety pitch (now split) |
| [Messaging/GraphQL](MESSAGING_GRAPHQL.md) | Queue and GraphQL feature planning |
| [Language](LANGUAGE.md) | Early language overview (superseded by Guide) |
