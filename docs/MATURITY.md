# Mako maturity taxonomy

Mako is alpha. To keep the docs honest, each public area should use one of
these labels instead of broad "done" claims.

| Label | Meaning | Required Evidence |
|-------|---------|-------------------|
| **Stable Alpha** | Intended for real user programs during 0.x, with breaking changes still possible | Positive tests, negative tests where relevant, docs, CI coverage, no known major platform gap |
| **Hardened** | Stable Alpha plus adversarial tests, sanitizer/race coverage, and a maintained regression gate | Stable Alpha evidence plus ASan/UBSan/TSan or equivalent focused gate |
| **Experimental** | Works for selected examples but API or implementation may change | Focused tests or demos, limitations documented |
| **Prototype** | Seed/proof of shape; useful for design exploration, not a product promise | Smoke test or example only |
| **Unsupported** | Not implemented or intentionally out of scope | Docs say what fails and what to use instead |

## Current Classification

| Area | Maturity | Notes |
|------|----------|-------|
| Core syntax, parser, typecheck | Stable Alpha | `fn`, `let`, structs/enums, generics, interfaces, tuples, match |
| Ownership and safe indexing | Hardened | Move checks, release bounds checks, ASan/UBSan gates; not formally proven |
| Structured concurrency | Hardened | `crew`, `kick`, `join`, channels, `select`; blocked C/FFI may delay cancellation |
| Backend-service workflow | Stable Alpha | HTTP/TLS/JSON/SQL/logging exist; end-to-end benchmark coverage is still being expanded |
| C backend | Stable Alpha | Mature compatibility path and sanitizer/cross/emit-c fallback |
| Native Cranelift backend | Stable Alpha | Default debug/native path; unsupported modes fail or fall back by policy |
| LLVM release backend | Experimental | Release optimizer path; requires feature build and workload-specific evidence |
| Package registry/signing | Experimental | Public registry exists; few packages; signing lacks revocation/key-rotation story |
| Stdlib core packages | Stable Alpha | `io`, `encoding/json`, `context`, `collections`, `net/http`, `database/sql` |
| Protocol/domain packs | Experimental | SIP/DTLS/Diameter/etc. are backend examples, not core product identity |
| GPU, local AI, storage engine, plugin host | Prototype | Keep as domain tracks until product-grade gates exist |
| WASM | Experimental | WASI Preview 1 only; no sockets/TLS/DOM |
| Debugger/DAP product | Experimental | lldb-backed launch exists; full debugger product remains open |
| Windows | Experimental | Known fixture failures and platform gaps remain |

## Documentation Rule

When a doc uses "done", it should either:

1. Link to evidence and fit **Stable Alpha** or **Hardened**, or
2. Say **Experimental** / **Prototype** and name the limitation.

The word "seed" maps to **Prototype** unless a status table explicitly promotes
it with evidence.
