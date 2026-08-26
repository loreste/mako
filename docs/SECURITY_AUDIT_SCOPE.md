# Makori Security Audit Scope

**Project**: Mako programming language — compiler + C runtime  
**Target**: White-hat penetration test / adversarial code review  
**Date**: 2026-07-26  
**Contact**: Lance Oreste

---

## Overview

Makori compiles `.mko` sources to native binaries via C (clang/zig). The attack
surface spans: a C runtime (~15 header files, ~40K LOC), a Rust compiler
(~38K LOC codegen), and a native bridge (FFI layer, ~9K LOC C). The runtime
handles networking (HTTP/1.1, HTTP/2, WebSocket, SIP, SCTP, TLS), structured
concurrency (crew/nursery), channels, and memory management (string interning,
box freelist, slice/map containers).

---

## Scope

### Tier 1 — Critical (memory safety, RCE, data loss)

| Area | Files | What to look for |
|------|-------|-----------------|
| **Box freelist** | `runtime/mako_rt.h:2475-2504` | Double-free, UAF, race on `mako_box_mu`. Singly-linked freelist with bins ≤512; `mako_box_free()` re-inserts without poison/canary. |
| **String ownership** | `runtime/mako_rt.h` (MakoString, mako_str_clone, mako_str_free, builder) | Double-free on builder (`mako_str_builder_free` has no guard). Channel string send transfers ownership with no runtime enforcement. |
| **Channel concurrency** | `runtime/mako_rt.h:4863-5660` | Mutex/condvar correctness on MakoChan, MakoChanStr, MakoChanPtr. Unbuffered rendezvous handoff (cap=0). No destructor (deliberate — verify UAF not possible via IR_DROP path). |
| **Native bridge type confusion** | `runtime/native_bridge.c` | `int64_t ↔ void*` casts everywhere. Pack cells (`mako_native_pack_get/set`) cast without validation. Interface dispatch uses unvalidated `tag` field. |
| **HTTP header parsing** | `runtime/mako_http.h:335-535` | Fixed 8KB `req` buffer, 64-byte `tmp` for Connection header. Off-by-one on keep-alive body reads. No per-connection timeout → slowloris. |
| **Temp file predictability** | `src/main.rs:2207, 4606` | `mako_run_{stem}`, `mako_{stem}.c` — pid-only or stem-only naming. Symlink race on shared `/tmp`. |

### Tier 2 — High (concurrency, DoS, protocol parsing)

| Area | Files | What to look for |
|------|-------|-----------------|
| **HTTP connection table race** | `runtime/mako_http.h:122-137` | Global `MakoHttpConn[1024]` with atomic count but no per-slot mutex. TOCTOU between `live` flag and count update. |
| **WebSocket frame parsing** | `runtime/mako_ws.h:261-430` | 64-bit payload length reconstruction. Fragmentation reassembly has no cumulative size cap → memory exhaustion. Off-by-one NUL write when `plen == cap`. |
| **HTTP/2 window tracking** | `runtime/mako_http.h:2851-2877` | Global stream windows, no mutex. Concurrent window consumption → signed underflow. HPACK decode reads past frame boundary on malformed input. |
| **TLS pool handle ABA** | `runtime/mako_tls.h:4545-4848` | Generation counter (`gen`) prevents stale handle reuse. Verify: can `gen` wrap to collide? `inflight`/`dying` deferred-free correctness under stress. |
| **SIP parsing** | `runtime/mako_sip.h` | Content-Length saturation to INT64_MAX. Via/Route header reconstruction with snprintf into fixed buffers. |
| **WebSocket globals** | `runtime/mako_ws.h:143-146` | `mako_ws_g_last_opcode`, `mako_ws_g_last_close_code` etc. are global statics written without synchronization → data race under concurrent recv. |

### Tier 3 — Medium (crypto, codegen correctness, resource limits)

| Area | Files | What to look for |
|------|-------|-----------------|
| **TLS certificate validation** | `runtime/mako_tls.h:1063, 1317, 2177` | `SSL_VERIFY_NONE` in client paths. Hostname verification via `X509_VERIFY_PARAM_set1_host` — verify error path doesn't silently skip. OpenSSL 3.x deprecated API usage (EC_KEY, RSA_new). |
| **Integer overflow in codegen** | `src/codegen/mod.rs:4033`, `runtime/mako_overflow.h` | Default mode is `Wrap`. Verify `Trap` mode actually aborts. Shift masking `& 63`. Const-fold uses `wrapping_*` — verify no UB in edge cases. |
| **Compiler shell execution** | `src/cc.rs:101-156`, `src/main.rs:4630` | Uses `Command::new()` (no shell). `MAKO_CFLAGS` split on whitespace → verify no injection via env var with embedded args. |
| **Connection slot exhaustion** | `runtime/mako_http.h:507-517` | 1024 fixed slots, no timeout, no backpressure. Slowloris / connection exhaustion. |
| **SCTP/Diameter** | `runtime/mako_sctp.h`, `runtime/mako_diameter.h` | Binary protocol parsing from network input. Verify length fields validated before memcpy. |
| **Framebuffer pixel overflow** | `runtime/mako_domain.h:1923` | `w * h` unchecked after 4096 clamp. Multiplication fits int64 at max, but verify clamp can't be bypassed. |

### Out of scope

- Makori language semantics (type system soundness, borrow checker correctness)
- LSP server (`src/lsp.rs`) — local IPC only, no remote attack surface
- GraphQL executor — operates on trusted in-process schema
- Documentation, CI pipeline, packaging scripts

---

## Engagement rules

1. **Read-only first pass**: catalog all findings before attempting exploitation.
2. **No production systems**: all testing against local builds. The compiler is
   the target, not any deployed service.
3. **Memory tooling required**: all reproducer programs must run clean under
   ASan (`--sanitize address`) and TSan (`--sanitize thread`). Findings without
   a sanitizer-confirmed reproducer are informational only.
4. **Reproducer format**: one `.mko` file per finding, plus expected vs actual
   behavior. If the bug is in the runtime C headers directly, a standalone C
   `main()` that `#include`s the header is acceptable.
5. **Severity rating**: use CVSS 3.1. Memory corruption with RCE potential =
   Critical. DoS = High. Info leak = Medium. Hardening gap = Low.
6. **Disclosure**: findings go to the maintainer (Lance Oreste) first. Public
   disclosure after fix lands and a release is cut.

---

## Known accepted risks

| Risk | Status | Rationale |
|------|--------|-----------|
| Channel has no destructor | Deliberate (417-H) | Naive free is UAF when spawned tasks outlive scope. Needs refcount or drain registry. Leak is safer than corruption. |
| `SSL_VERIFY_NONE` in dev paths | Documented | Development/lab convenience. Production paths use `SSL_VERIFY_PEER`. |
| Global HTTP/WS state without mutex | Known | Single-threaded accept loop by design. Multi-threaded server requires user-side synchronization. Needs doc or enforcement. |
| Box freelist is global-locked | Performance trade | Single `mako_box_mu` for all sizes. Per-size-class locks if throughput matters. |

---

## Deliverables

1. **Finding report**: one page per finding — description, severity, reproducer,
   recommended fix.
2. **Reproducer archive**: `.mko` and/or `.c` files that trigger each finding
   under ASan/TSan.
3. **Summary table**: finding ID, severity, file:line, status (confirmed /
   informational / false positive).
4. **Hardening recommendations**: prioritized list of changes, estimated diff
   size, and whether each is a breaking change.

---

## Build & test instructions

```bash
# Build compiler
cargo build

# Run a .mko program
cargo run -- run path/to/file.mko

# Build with address sanitizer
cargo run -- build --sanitize address path/to/file.mko -o /tmp/test_asan
/tmp/test_asan

# Build with thread sanitizer
cargo run -- build --sanitize thread path/to/file.mko -o /tmp/test_tsan
/tmp/test_tsan

# Run test suite
cargo test                              # Rust unit tests (146 as of 2026-08-19)
cargo run -- test examples/testing/     # Makori integration tests
```

Runtime headers live in `runtime/` and are installed to
`~/.local/share/mako/runtime/` at build time.
