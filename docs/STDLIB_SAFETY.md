# Standard library memory safety gate

Mako is its own language with its own syntax and ownership model. Standard
library parity with Go or Rust means capability parity for safe application
work, not syntax cloning and not exposing raw-memory escape hatches.

The target is **100% memory-safe safe Mako**:

- safe Mako code cannot corrupt memory, use freed storage, double-free, race on
  ordinary mutable state, or index out of bounds;
- stdlib APIs either preserve that contract by construction or are not part of
  the safe surface;
- C, OpenSSL, zlib, OS, plugin, and FFI-backed implementations are acceptable
  only behind checked wrappers with explicit ownership and lifetime contracts;
- unsafe or unverifiable behavior must be excluded, isolated behind an explicit
  unsafe boundary, or marked blocked.

## Safety tiers

| Tier | Meaning | Release claim |
|------|---------|---------------|
| `safe` | Implemented in safe Mako or compiler-owned runtime primitives with bounds, ownership, and drop rules checked by tests | Can be part of the default stdlib |
| `checked-native` | Backed by C/OS/crypto/compression code, but wrapped so inputs are bounded, returned storage is owned, errors clean up, and handles cannot be double-freed from safe Mako | Can be part of the default stdlib after sanitizer and negative tests |
| `unsafe-boundary` | Requires raw pointers, unchecked indexing, FFI ownership transfer, dynamic library calls, mmap aliasing, or platform behavior the compiler cannot prove | Must be explicit, narrow, documented, and opt-in |
| `blocked` | Cannot meet the safe Mako contract yet | Not part of safe stdlib parity |
| `won't` | Conflicts with Mako identity or safety goals | Intentionally excluded |

## Package acceptance checklist

A stdlib package is not `Done` for safe parity until all applicable items pass:

- **Ownership:** every returned string, slice, map, handle, and struct has one
  documented owner; borrowed views cannot escape their owner.
- **Cleanup:** every error, early return, and partial parse frees temporary
  allocations and closes temporary native handles.
- **Bounds:** all reads and writes validate lengths before touching memory,
  including malformed binary input and integer overflow in size calculations.
- **Handle lifecycle:** native handles have idempotent close/drop behavior, stale
  handles are rejected, and close-while-in-use is synchronized.
- **Concurrency:** shared state uses channels, `Mutex`, `RWMutex`, `CMap`,
  or atomics; ordinary `let mut` state cannot be shared across tasks.
- **Footgun prevention:** unsafe or insecure behavior is not the default API.
  If a helper disables verification, skips bounds, accepts raw SQL, loads
  dynamic code, or transfers native ownership, the name must say so, docs
  must point to the safe alternative, and the package must fail closed when
  safe preconditions are missing.
- **Backend parity:** safe behavior is the same on C, native, and LLVM backends,
  or the unsupported backend hard-errors.
- **Verification:** package-specific tests include success cases, malformed
  input, boundary sizes, repeated cleanup, and sanitizer runs for native-backed
  code.

The static claim audit is enforced by `scripts/stdlib-safety-audit.sh`. It must
pass before a release can claim stdlib safety coverage: every checked-in
`std/**/*.mko` file must appear in the safety matrix, no matrix row may point at
a missing package, checked-native and unsafe-boundary rows must name test work,
all native/unsafe rows must map to one safety contract family in
[STDLIB_SAFETY_CONTRACTS.md](STDLIB_SAFETY_CONTRACTS.md), and the high-risk
adversarial/lifecycle fixtures must stay present.

## Go and Rust parity rule

Mako does not need equivalents for memory-unsafe surfaces to be on par for safe
application programming.

| Ecosystem surface | Mako decision |
|-------------------|---------------|
| Go `unsafe` | `won't` for safe stdlib |
| Go `go/*` compiler/toolchain packages | only pure data parsers if memory-safe and useful |
| Go `debug/*` binary parsers | `blocked` until bounded, fuzzed, and sandbox-safe |
| Rust raw pointers / `unsafe` APIs / allocator internals | `won't` for safe stdlib |
| Rust unchecked indexing / uninitialized memory primitives | `won't` for safe stdlib |
| Rust FFI/dynamic loading equivalents | `unsafe-boundary` unless wrapped as checked handles |

## Current blockers before claiming 100%

The repo can advertise broad stdlib coverage, but **not complete memory-safe
stdlib parity** until these are closed:

- every `checked-native` package maps to an enforced safety contract family and
  has negative/lifecycle fixture coverage;
- parser/codec packages have malformed-input coverage and overflow guards;
- dynamic/plugin/syscall/mmap-like surfaces are separated from the default safe
  claim unless their handles are fully checked;
- sanitizer gates cover the native-backed stdlib paths they claim to protect;
- native-backend full-suite reporting gaps are either fixed or explicitly kept
  out of the default safe-stdlib claim until their package rows are hardened.

The default product message should be: Mako is fast, Mako-shaped, and safe by
construction; parity work expands capability only when it preserves that bar.

Package-by-package status is tracked in
[STDLIB_SAFETY_MATRIX.md](STDLIB_SAFETY_MATRIX.md).
Contract families are tracked in
[STDLIB_SAFETY_CONTRACTS.md](STDLIB_SAFETY_CONTRACTS.md).
