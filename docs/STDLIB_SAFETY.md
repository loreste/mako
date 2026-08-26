# Standard library memory safety gate

Makori is its own language with its own syntax and ownership model. Standard
library parity with Go or Rust means capability parity for safe application
work, not syntax cloning and not exposing raw-memory escape hatches.

The target is **100% memory-safe safe Mako**:

- safe Makori code cannot corrupt memory, use freed storage, double-free, race on
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
| `checked-native` | Backed by C/OS/crypto/compression code, but wrapped so inputs are bounded, returned storage is owned, errors clean up, and handles cannot be double-freed from safe Mako | Can be part of the default-safe claim only after package-local evidence, contract-family mapping, malformed/lifecycle tests, and sanitizer gates pass |
| `unsafe-boundary` | Requires raw pointers, unchecked indexing, FFI ownership transfer, dynamic library calls, mmap aliasing, process/syscall control, or platform behavior the compiler cannot prove | Excluded from the default-safe claim until hardened and reclassified |
| `blocked` | Cannot meet the safe Mako contract yet | Not part of safe stdlib parity |
| `won't` | Conflicts with Makori identity or safety goals | Intentionally excluded |

## Default-Safe Claim

The checked-in 0.5.15 stdlib matrix covers **144** package files:

- **38 `safe`** files are in the default-safe claim.
- **102 `checked-native`** files are in the default-safe claim only when
  `scripts/stdlib-safety-audit.sh`, the package-local evidence fixture,
  malformed/lifecycle tests, and sanitizer gates pass.
- **4 `unsafe-boundary`** files are explicitly excluded from the default-safe
  claim: `std/os/exec`, `std/plugin`, `std/runtime`, and `std/syscall`.
- **0 `blocked` / `won't` checked-in std files** are claimed safe. Historical
  or ecosystem-equivalent exclusions remain listed in
  [STDLIB_SAFETY_MATRIX.md](STDLIB_SAFETY_MATRIX.md).
- **External** dependencies, host services, dynamic libraries, network peers,
  OS behavior, GPU drivers, databases, and remote APIs are environmental inputs.
  They may be used by checked wrappers, but they are never proof for a default
  safe claim unless a hard local/CI gate exercises the wrapper and fail-closed
  path.

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
  code. Every checked-native package must also have package-local safety
  evidence in `examples/testing/stdlib_package_local_safety_test.mko`.

The static claim audit is enforced by `scripts/stdlib-safety-audit.sh`. It must
pass before a release can claim stdlib safety coverage: every checked-in
`std/**/*.mko` file must appear in the safety matrix, no matrix row may point at
a missing package, checked-native rows must name test work and package-local
evidence, unsafe-boundary rows must carry explicit excluded-from-default-safe
evidence, all native/unsafe rows must map to one safety contract family in
[STDLIB_SAFETY_CONTRACTS.md](STDLIB_SAFETY_CONTRACTS.md), and the high-risk
adversarial/lifecycle fixtures must stay present.

## Go and Rust parity rule

Makori does not need equivalents for memory-unsafe surfaces to be on par for safe
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
  has package-local evidence plus negative/lifecycle fixture coverage;
- parser/codec packages have malformed-input coverage and overflow guards;
- package-local parser/codec malformed-input fixtures stay enforced by
  `scripts/stdlib-safety-audit.sh`;
- dynamic/plugin/syscall/mmap-like surfaces stay outside the default-safe claim
  unless handles are fully checked and the package is reclassified;
- sanitizer gates cover the native-backed stdlib paths they claim to protect;
- native-backend full-suite reporting gaps are either fixed or explicitly kept
  out of the default safe-stdlib claim until their package rows are hardened.

The default product message should be: Makori is fast, Mako-shaped, and safe by
construction; parity work expands capability only when it preserves that bar.

Package-by-package status is tracked in
[STDLIB_SAFETY_MATRIX.md](STDLIB_SAFETY_MATRIX.md).
Contract families are tracked in
[STDLIB_SAFETY_CONTRACTS.md](STDLIB_SAFETY_CONTRACTS.md).
