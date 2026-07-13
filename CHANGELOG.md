# Changelog

## 0.1.0 — 2026-07-13 (wave 21 queue)

- Nested `Ok(Ok(x))` typechecks against inner Result expected type
- `Result[Result[T, E], E2]` Ok box/unbox + match; `wrap_ok(Ok(...))` mono
- Regex `\p{Canadian}` / Deseret / Phoenician seeds
- TSan CI: `wave20_queue_test`, `chan_float_test`
- Tests: `examples/testing/wave21_queue_test.mko`

## 0.1.0 — 2026-07-13 (wave 20 queue)

- Multi-layer Option nest chains (triple+ string/int; `Result[Option³[T]]`)
- Stable `Type::mono_tag` for Option/Result; `Some(...)` mono tag alignment
- Mid-label continue NLL bad case; Braille/Ogham/Gothic script seeds
- TSan CI: `wave19_queue_test`, `chan_struct_test`
- Tests: `examples/testing/wave20_queue_test.mko`

## 0.1.0 — 2026-07-13 (wave 19 queue)

- `Option` containers (`[]int`, maps) and `Option[Option[T]]` nesting
- Nested `Result[Option[Option[T]], E]` Ok + match unbox chain
- `jpeg_has_sof0` marker scan for JFIF/SOF0 shell
- Regex `\p{Thaana}` / `\p{Tagalog}` / `\p{Bopomofo}` seeds
- TSan CI: `wave18_queue_test`, `chan_string_test`
- Tests: `examples/testing/wave19_queue_test.mko`

## 0.1.0 — 2026-07-13 (wave 18 queue)

- Generic `Option[T]` Some: string/float/ptr payloads (not only int)
- Nested `Result[Option[T], E]` Ok (boxed option + match unbox)
- Regex `\p{Syriac}` / `\p{Coptic}` / `\p{Runic}` seeds
- TSan CI: `wave17_queue_test`
- Tests: `examples/testing/wave18_queue_test.mko`

## 0.1.0 — 2026-07-13 (wave 17 queue)

- Generic `Result[T]` mono tags aligned for arrays/maps (`arr_*` / `map_*`)
- Peek C types for array/map/struct lits so match Ok unboxes correctly
- Multi-label NLL bad case: continue+break outer field product
- Regex `\p{Myanmar}` / `\p{Khmer}` / `\p{Tibetan}` seeds
- TSan CI: `wave16_queue_test`, `share_atomic_test`
- Tests: `examples/testing/wave17_queue_test.mko`

## 0.1.0 — 2026-07-12 (wave 16 queue)

- Break-path NLL: move+break poisons post-loop (partial if + labeled outer)
- Generic `Result[T, E]` Ok: match/let resolve monomorphized ok kind
- Regex `\p{Bengali}` / `\p{Sinhala}` seeds
- TSan CI: `wave15_queue_test`
- Tests: `examples/testing/wave16_queue_test.mko`

## 0.1.0 — 2026-07-12 (wave 15 queue)

- `Result[[]Struct, E]` Ok (boxed `MakoArr_*`)
- Labeled `continue outer` records NLL moves on the outer loop frame
- Regex `\p{Georgian}` / `\p{Cherokee}` seeds
- TSan CI: `wave14_queue_test`
- Tests: `examples/testing/wave15_queue_test.mko`

## 0.1.0 — 2026-07-12 (wave 14 queue)

- `Result[[]string, E]` and `Result[[]float, E]` Ok (boxed arrays)
- NLL loop × match partial products (`hold_loop_match_partial`, `_product_exit`)
- Regex `\p{Tamil}` / `\p{Armenian}` / `\p{Ethiopic}` seeds
- TSan CI: `fan_string_test`, `kick_string_test`
- Tests: `examples/testing/wave14_queue_test.mko`

## 0.1.0 — 2026-07-12 (wave 13 queue)

- `Result[map[int]int, E]` and `Result[map[string]string, E]` Ok
- `reflect_value_of` flattens nested POD structs
- NLL match / nested-path partial products (bad examples)
- Regex `\p{Thai}` / `\p{Devanagari}` seeds
- TSan CI: `kick_sync_test`, `wave11_queue_test`
- Tests: `examples/testing/wave13_queue_test.mko`

## 0.1.0 — 2026-07-12 (wave 12 queue)

- `Result[map[string]int, E]` Ok via `mako_ok_ptr` (map pointer)
- Bad: non-POD kick (`kick_non_pod`), nested reflect (`reflect_non_pod`)
- NLL if/else partial-field product (`hold_if_else_partial_product`)
- Regex `\p{Hiragana}` / `\p{Katakana}` / `\p{Hangul}` seeds
- JPEG JFIF docs: external header + APP7 Mako payload
- Tests: `examples/testing/wave12_queue_test.mko`

## 0.1.0 — 2026-07-12 (wave 11 queue)

- Flatten `join_timeout` for `Job[Result[T, string]]` (no nested Result)
- Hardened timeout tests (longer sleep / shorter deadline)
- POD kick allows string fields (cloned into heap box)
- `reflect_value_of` snapshots all POD fields (not only two ints)
- `Result[[]int, E]` Ok via heap-boxed array
- Tests: `examples/testing/wave11_queue_test.mko`

## 0.1.0 — 2026-07-12 (wave 10 queue)

- `job.join_timeout(ms)` always returns `Result[R, string]` (`Err("timeout")`)
- Kick allows **POD structs** (int/float/bool fields), heap-boxed
- `reflect_value_of(struct)` for POD 2-field snapshots
- C-style `for` NLL loop-carried fixpoint
- SMTP TLS: `MAKO_SMTP_TLS_VERIFY=1` enables peer cert check
- TSan CI includes `proxy_edge_test`
- Tests: `examples/testing/wave10_queue_test.mko`

## 0.1.0 — 2026-07-12 (wave 9 queue)

- Struct channel **select** (`mako_chan_ptr_selectn`); arm must not `recv` again
- `Result[Struct, E]` Ok via heap `mako_ok_ptr`
- `join_timeout` for Result → `Err("timeout")`; string → empty
- SMTP STARTTLS path runs **SSL_connect** when OpenSSL is linked
- `reflect_value_from_2_int`; regex Mark/Nl/No seeds
- Docs: kick Send rules in SECURITY; STATUS/ROADMAP wave 9
- Tests: `examples/testing/wave9_queue_test.mko`

## 0.1.0 — 2026-07-12 (wave 8 queue)

- Real `join_timeout` (poll task done; return 0 if still running)
- String channel select (`chan_str_select2`, `chan_select_value_str`)
- `Result[float, E]` Ok; enum Err packs i0–i2 and s0–s1
- Crew NLL keep outer hold moves; kick float bitcast
- `reflect_value_from_2`; regex `\p{Z}` / `\p{Sc}` / scripts
- SMTP AUTH continues after STARTTLS when OpenSSL linked
- CI: expanded TSan suite; informational bench-gate 1.5×
- Tests: `examples/testing/wave8_queue_test.mko`

## 0.1.0 — 2026-07-12 (concurrency / result / CI)

- `job.join` for string and `Result` returns (heap-box across kick)
- `Result[string, E]` Ok via `ok_s` / `mako_ok_str`
- `chan_open[float]`, `fan` on `[]Struct`, TCP pool mutex
- select arm NLL join; recovery multi-error hints
- `log_*` / slog emit active `trace=` id
- CI: `bench-gate` and TSan concurrency smoke jobs
- Docs: BUILTINS §§23–24/71–75, GUIDE crew/channels, STDLIB sync, SPEED,
  ROADMAP, DEBUG TSan, PERFORMANCE bench-gate CI note

## 0.1.0 — 2026-07-12 (wiring audit)

- **`would_overflow_sub`** — fully wired (types + codegen + docs + tests); was runtime-only.
- **Parser recovery** — `recover_to_next_decl` no longer consumes the next item's start
  keyword; unit test proves following good `fn`s stay in the AST.
- **`BuildOpts`** — test path fills `overflow` / `bounds_always` (cargo test compiles).
- Removed unused `fold_const_c` wrapper (fold path is `fold_const_c_env` only).

## 0.1.0 — 2026-07-12 (module layout)

### Compiler modules (implementation order)

1. `src/overflow.rs` + `runtime/mako_overflow.h` + codegen trap path
2. `src/recovery.rs` + multi-error emit via `diag`
3. `src/shutdown.rs` + `runtime/mako_shutdown.h`
4. `runtime/mako_rt.h` — `MAKO_BOUNDS_CHECK` / `MAKO_BOUNDS_ALWAYS`
5. `src/errors.rs` — `Result[T, Enum]` helpers for codegen
6. `src/leak.rs` + `runtime/mako_leak.h`

## 0.1.0 — 2026-07-12 (complete: Result enum, const fn, crew drain)

### Language / runtime (completion pass)

- **`Result[int, Enum]`** — `Err(MyError::…)` packs enum tag/payload; `match Err(e)`
  reconstructs the enum for nested match.
- **`const fn`** — parse + fold at typecheck/codegen; `const X = f(…)` works.
- **`crew.drain(ms)`** / `crew_drain` — cancel+join with timeout budget.
- **`evloop_shutdown`** — free event loop.
- **NLL** — fold more integer comparisons for dead-edge pruning.
- Tests: `result_enum_test.mko`, `const_fn_test.mko`, `crew_drain_test.mko`.

## 0.1.0 — 2026-07-12 (overflow, shutdown, recovery, leak, trace)

### Compiler / runtime safety

- **Checked arithmetic** — `runtime/mako_overflow.h`; `checked_add` / `checked_sub` /
  `checked_mul`, `would_overflow_*`. CLI `--overflow trap|wrap|ignore` (build/run).
  Trap mode emits `mako_add_i64` etc. for `+ - *` on ints.
- **Parser multi-error recovery** — `parse_with_errors` + `recover_to_next_decl`;
  `mako check` reports all top-level parse errors (`examples/bad/multi_error.mko`).
- **Graceful shutdown** — `signal_on_term`, `register_listener` / `close_listeners`,
  `server_shutdown_begin`, `server_drain`, `shutdown_requested`,
  `install_graceful_shutdown` (`runtime/mako_shutdown.h`).
- **Leak scopes** — `leak_scope_enter` / `leak_scope_exit` / `leak_check` on top of
  alloc tracking (`runtime/mako_leak.h`).
- **Tracing seed** — `trace_id` / `trace_set` / `trace_begin` / `trace_end` /
  `trace_log` (`runtime/mako_trace.h`).
- **`mako dev`** — watch source mtime and rebuild+rerun (hot-reload seed).
- **`--bounds always`** on build/run keeps bounds checks under release.

Tests: `examples/testing/overflow_shutdown_test.mko`.

## 0.1.0 — 2026-07-12 (proxy hot path)

### Networking / reverse-proxy runtime

- **TCP connection pool** — `tcp_pool_open` / `acquire` / `release` / `close`
  keeps backend fds per host:port, validates before reuse, closes on error.
- **`http_forward_full`** — returns `HttpForwardResult` with status, body,
  body length, and total bytes; supports Content-Length, chunked, and close.
- **`http_proxy_raw`** — raw request → backend → raw response → client pump.
- **`http_parse`** — C hot-path request parser (`HttpParsed`: method/path/host/
  headers/body/chunked) without Mako `str_split` allocations.
- **Chunked decode** — `http_decode_chunked` + integrated in forward/proxy.
- **Nonblocking connect** — `tcp_connect_nb` / `connect_check` / `connect_wait`.
- **fd-to-fd copy** — `tcp_fd_copy` / `tcp_splice` (Linux `splice`) / `tcp_proxy_pump`.
- **Socket tuning** — `tcp_listen_reuseport`, `tcp_set_recv_buf`/`send_buf`,
  `tcp_accept4` (`NONBLOCK|CLOEXEC`).
- **Async TLS accept** — `tls_accept_start` / `tls_handshake_step` /
  `tls_want_read`/`write` / `tls_read_nb`/`write_nb` for worker-friendly handshakes.
- **HTTP/2 multiplexing** — 32 stream slots, ready queue
  (`http2_next_ready_stream` / `stream_take` / `stream_body`), concurrent bodies.
- **HTTP/3 surface** — `h3_server_new` / `bind` / `poll` / `accept_stream` /
  `stream_read`/`write` (UDP event integration; crypto depth via quiche).
- Tests: `examples/testing/proxy_pool_test.mko`.
- **Edge cases** — request builder normalizes caller headers (trailing CRLF),
  skips duplicate Host/Content-Length; chunked supports extensions/trailers/
  bare LF/incomplete→empty; parse truncates to Content-Length, LF-only headers,
  case-insensitive lookup; no-body statuses (1xx/204/304); pool/connect/proxy
  bad-arg guards; nonblocking reusable peek. Tests:
  `examples/testing/proxy_edge_test.mko`. Docs: BUILTINS *Reverse-proxy notes*,
  STDLIB pool section, book ch08 reverse proxy / mux / async TLS / H3.

## 0.1.0 — 2026-07-12 (networking & auth)

### Networking

- **Reverse proxy** — `http_forward(host, port, method, path, body)` forwards a
  request to an upstream HTTP/1.1 backend and returns the response body. With
  the HTTP/2 server this is a complete reverse proxy
  (`examples/h2_reverse_proxy.mko`), verified `curl --http2` → proxy → backend.
- **Bind-address control** — `tcp_listen_addr(host, port)` binds a specific
  address (loopback-only, a chosen NIC, or `"*"` for all). Verified on Linux.
- **Session controls** — `tcp_set_timeout(fd, ms)` (recv/send timeouts),
  `tcp_keepalive(fd, idle, interval, count)` (dead-peer detection),
  `tcp_listen_backlog(host, port, backlog)` (bound the accept queue).
- **Socket-style TLS server** — `tls_server_new` / `tls_accept` / `tls_read` /
  `tls_write` / `tls_conn_alpn` (ALPN `h2`), plus `tls_server_new_tls13` to
  require TLS 1.3. Verified on Linux: a 1.2 client is rejected with a
  protocol_version alert, a 1.3 client negotiates `TLS_AES_256_GCM_SHA384`.

### Security / auth

- **bcrypt** — `crypto.bcrypt(password, cost)` / `bcrypt_check` / `bcrypt_ok`
  (`$2b$` via libxcrypt on Linux; Argon2id remains the recommendation for new
  systems). Verified on Linux against a round-trip with distinct salts.
- **SCRAM-SHA-256** — `crypto.scram_*` toolkit (salted password, client/server
  keys, stored key, signatures, client proof, server-side proof verification)
  plus raw primitives `sha256_raw`, `hmac_sha256_raw`, `xor_bytes`. Verified
  byte-for-byte against the RFC 7677 test vector.

### Fixes

- Runtime header include order (`mako_net.h` before `mako_http.h`) so
  `mako_bind_ipv4_addr` is declared before use.
- `game_udp_bind` now calls the address helper with a wildcard host.

## 0.1.0 — 2026-07-12 (expressions & assignment)

### Language

- **`if` as an expression** — `let x = if c { a } else { b }`; each branch yields
  its trailing expression, `else` required, both branches must agree on type
- **Parallel binding & assignment** — `var a, b = 1, 2` and `a, b = b, a`
  (swap/rotate); the right-hand side is evaluated before any target is written

### Security / stdlib

- **Password hashing** — `crypto.password_hash` / `password_verify` (Argon2id,
  OWASP parameters, PHC string format) backed by OpenSSL's trusted implementation

### Concurrency

- **Sendable synchronization handles** — `CMap`, `Mutex`, `RWMutex`, and
  `AtomicInt` may now be passed into a kicked task; the same object is shared
  (matching the documented CMap behaviour). Structs / arrays / arenas stay
  non-sendable. The `kick` error hint now lists them.

### Security / KDF

- **PBKDF2-HMAC-SHA256** — `crypto.pbkdf2` / `pbkdf2_sha256(password, salt,
  iterations, dklen)`, verified against published test vectors. Completes the
  SCRAM-SHA-256 primitive set (with `hmac_sha256`, `sha256`, `random_bytes`,
  `const_eq`).

### HTTP/2

- **Per-connection state handles** — `http2_conn_new` / `http2_conn_use` /
  `http2_conn_free`. A server or proxy can juggle several HTTP/2 connections on
  one thread; each keeps independent stream/settings/flow-control state. Leaving
  the handles unused keeps the original single-connection behaviour.
- **fix: server can read client requests** — `http2_conn_recv` rejected the
  client's odd-numbered streams in server mode (inverted stream-id parity), so no
  request ever assembled. A received HEADERS now correctly opens a client
  (odd-id) stream, so `header_block` + HPACK decode recover `:method` / `:path` —
  the basis for an H2 accept loop / reverse proxy.
- **`http2_response(stream, status, body)`** — builds a full response (HEADERS
  with `:status` + `content-length`, then DATA with END_STREAM) in one call,
  completing the read-request → write-response cycle for an H2 server.
- **HPACK decode handles real clients** — the decoder now does Huffman-encoded
  strings, indexed names, incremental indexing, never-indexed literals, varint
  lengths, and the full 61-entry static table (previously a partial table with
  wrong indices and no Huffman). A complete H2-over-TLS server built from these
  primitives is verified end-to-end against `curl --http2`, routing by `:path`.
  Example: `examples/h2_dynamic_server.mko`.
- **HTTP/2 reverse proxy** — `http_forward(host, port, method, path, body)`
  forwards a request to an upstream HTTP backend and returns its response body.
  Composed with the H2 server, `examples/h2_reverse_proxy.mko` is a complete
  reverse proxy verified end-to-end: `curl --http2` → Mako proxy → backend →
  relayed response.

### TLS

- **Socket-style TLS server API** — `tls_server_new(cert, key)`, `tls_accept(fd)`,
  `tls_read` / `tls_write`, `tls_conn_alpn`, `tls_conn_close`. Own the accept
  loop and upgrade an accepted TCP fd to TLS — including STARTTLS-style upgrades
  on the same socket (verified against Postgres-style `SSLRequest` negotiation).
  ALPN negotiates `h2` / `http/1.1` for proxy use.

### OS

- **Signal hooks by name** — `signal_watch("HUP")`, `signal_fired("HUP")`,
  `signal_ignore("PIPE")` for HUP/TERM/INT/USR1/USR2/QUIT/PIPE/CHLD. Distinct
  per-signal flags (reload vs shutdown), and handlers interrupt blocking calls so
  an accept loop can react.
- **File-system watch** — `watch_new` / `watch_add(path)` / `watch_poll(timeout)`
  / `watch_close` over kqueue (macOS/BSD) and inotify (Linux). `watch_poll`
  returns the path that changed. Pairs with SIGHUP for config reloads.

### Networking

- **UDP request/response routing** — `game_udp_sender_addr` (the `host:port` of
  the last sender) and `game_udp_send_to` (send to an arbitrary address). Enables
  forwarding traffic upstream and routing replies back to the original sender.
- multiple UDP frontends by wrapping a `GameUDP` handle in a struct (struct
  arrays hold handles); array literals accept a trailing comma.

### Examples

- **non-blocking multi-client server** — `examples/nb_echo_server.mko`, a complete
  one-thread reactor over `evloop_*` + `nb_*` (accept many clients, service the
  ready ones). Verified with concurrent clients — the template for a protocol
  server such as pgwire.

### Tooling

- **test failures explain themselves** — when a test process crashes it now
  reports the terminating signal (e.g. "killed by signal 11 (SIGSEGV)") and says
  it is a runtime fault rather than a failed assertion, instead of a bare `FAIL`
  with no detail. Assertion failures already print their own message.

### Fixes

- **build cache** — object cache keys now include a fingerprint of the bundled
  runtime headers, so updating the compiler (or editing a `runtime/*.h`)
  invalidates stale `.o` objects even when the generated C is byte-identical.
  Previously a runtime change could be masked by a cached object until the cache
  was cleared by hand.
- array of positional struct literals `[P{1}, P{2}]` now compiles as a struct array
- inline tuple literals emit their typedef when first seen in a function body
- identifiers that shadow C/POSIX library names (`read`, `write`, `time`, …) now
  emit valid, linkable C

- Tests: `if_expr_test`, `parallel_assign_test`, `password_hash_test`

---

## 0.1.0 — 2026-07-12 (control-flow surface)

### Control flow & statements

- **`if init; cond { … }`** — init clause scoped to the if/else
- **`switch` / `case` / `default`** — value, expression-less, and init forms;
  arbitrary case expressions, single tag evaluation, optional default
- **`for` (four forms)** — three-clause `for i := 0; i < n; i++`, condition-only
  `for cond {}`, infinite `for {}`, plus range `for i, v in range xs`
- **Compound assignment & inc/dec** — `+= -= *= /= %=` and `++` / `--` on
  identifiers, struct fields, and index targets
- **Positional struct literals** — `Point{1, 2}` and zero-value `Point{}`;
  composite-literal-in-condition ambiguity resolved
- **`go f()`** — schedules a call onto the innermost `crew` (errors outside one)
- A function body that provably returns on every path is accepted with no
  trailing `return`

### Fixes

- Identifiers colliding with C keywords (`let switch = 1`, params named `int`, …)
  now emit valid C — codegen mangles reserved words consistently
- `pack` / `pull` / `switch` / `go` are contextual keywords — usable as names
- Labeled `break` / `continue` only bind a label on the same source line
- `mako fmt` no longer doubles `export` on structs

- Tests: `if_init_test`, `switch_test`, `for_forms_test`, `compound_assign_test`,
  `struct_positional_test`, `go_stmt_test`, `parallel_assign_test`

---

## 0.1.0 — 2026-07-11 (gap close wave 6)

### Struct channels · tagged errors

- **`chan_open[Point]`** — `MakoChanPtr` heap-box send / unbox recv
- **`error_tag("NotFound", "user")`** — enum-like string error tags
- Tests: `chan_struct_test`, `error_tag_test`

### Waves 1–5

- Send-like kick · ShareInt/string pack · atomic share · fan int/float/string ·
  visibility · error_context/join · bench-gate · lint --identity

---

## 0.1.0 — 2026-07-11 (path-style import blocks)

### Imports — service-scale groups

- Nested std: `"encoding/json"`, `"net/http"`, `"path/filepath"`, …
- Module paths: `module = "izi-iva"` → `"izi-iva/pkg/acd"`; `vendor/<path>/`; `[dependencies]` keyed by import path
- Alias form: `redisv9 "github.com/…"` (and `"path" as name`)
- Blank lines inside `import (` / `pull (` groups
- Fix: prefix rewrite no longer rewrites params/locals that share fn names (`body`, `path`, …)
- Fix: `encoding/json`, `errors`, `net/http` importable; seed packs for `crypto/tls`, `os/signal`, `syscall`, `net`
- Example: `examples/import_paths/` · test: `import_paths_test.mko`

---

## 0.1.0 — 2026-07-11 (low ceremony + pain map + flair)

### Product — real work, less typing

- New [docs/ERGONOMICS.md](docs/ERGONOMICS.md): happy path stays short (infer locals, one `print`, string `==`, `match` routes, opt-in power)
- Tests: `examples/testing/ergonomics_test.mko`
- Canonical sample updated: `examples/mako_style.mko`
- Pillar wired into VISION / IDENTITY / AGENTS / README

### Product — Go/Rust pain → Mako answers

- [docs/PAIN_POINTS.md](docs/PAIN_POINTS.md): honest map of Go/Rust pain vs Mako tools
- Residuals queued (races, richer errors, NLL, visibility, identity lint)
- Identity rule unchanged: unique language, unique syntax — not a clone

### Units — Done

- **Preferred flair:** `pack name` · `pull "path"` · `pull "path" as name` · `pull ( … )`
- Dual: `package` / `import` (all previous forms still parse)
- Always pack-qualify normal pulls: `pkg.fn(...)` (internal `pkg__fn`)
- Default name from `pack` clause (≠ `main`), else path basename
- `mako fmt` emits `pack` / `pull` / `"path" as name`
- Prefix rewrite splits value vs type names (`fmt.int` safe)
- Identity: `docs/IDENTITY.md` flair table · **~90%**

---

## 0.1.0 — 2026-07-11 (docs + syntax identity)

### Mako-owned syntax (Done)

- **Preferred** forms are Mako-native: `fn`, `let`, `struct`, `on Type`, `hold`/`share`/`arena`, `crew`/`kick`, `export`, `match`
- Dual Go-like spellings (`func`, `:=`, `var`, bare `a int`, receivers) remain **compat sugar only**
- Identity doc + checklist: `docs/IDENTITY.md` (**~86%** identity strength)
- Dual-form inventory: `docs/GO_SYNTAX_CHECKLIST.md` (optional; not preferred)
- Canonical sample: `examples/mako_style.mko` · `mako fmt` emits Mako-native spellings
- Docs re-centered: GUIDE, LANGUAGE, COMPAT, STATUS, book, README, llms.txt

### Language wave 10 (Done)

- User generics monomorphization, `on Type` methods, tuples, typed `chan_open[T]`
- Compat policy: `docs/COMPAT.md`

---

## 0.1.0 — 2026-07-10

STATUS north-star / MVP: **100%** (homebrew-core publish remains an external blocker).

### The Mako Book + docs accuracy pass (Done)

- New guided book under `docs/book/` (15 chapters + `SUMMARY.md` + optional mdBook `book.toml`)
- Checkable samples: `docs/book/examples/book_{hello,ops,errors,imports}.mko`
- Cross-links from README, GUIDE, LANGUAGE, KEYWORDS, STDLIB, STATUS, ROADMAP, VISION, howto/
- GUIDE “Target” section corrected (CFG NLL / H2 / WASI preview1 already Done)
- Suite: **130 passed**, 0 failed · stdlib ~**98%** major areas

### Stdlib Wave 9 regexp increment (Done)

- Regexp `\p{...}` now decodes UTF-8 and recognizes common categories/scripts (`L`, `N`, `Nd`, Latin, Greek, Cyrillic, and several major letter/digit ranges).
- Added simple zero-width lookahead support: `(?=...)` and `(?!...)`.
- Tests: `goext_wave9_test.mko`
- Suite: **130 passed**, 0 failed
- Honest stdlib coverage: **~98%** of major standard library *areas*

### General-purpose package offline/private registry increment (Done)

- `mako pkg install`, `lock`, and `update` now support `--offline`.
- Offline package resolution uses local path deps, cached git deps in `.mako/deps`, and `.mako/registry` / `$MAKO_REGISTRY`, then fails fast instead of fetching.
- Tests: `offline_git_requires_cached_dep`
- Product intention: Toolchain/IDE track **100%**.

### Stdlib Wave 8 + CLI polish (Done)

- RE2 backrefs `\1`–`\9` · `\p{L}`/`\p{N}` (ASCII) · `[:lower:]`/`[:upper:]`/`[:punct:]`
- JFIF grayscale encode/detect; codegen reflect type schema registry
- SMTP STARTTLS soft path; `str_cut` / `str_count`
- CLI: richer `build`/`run`/`check`/`test` help; `version` listed before `lsp`; after_help
- Tests: `goext_wave8_test.mko`
- Suite: **129 passed**, 0 failed
- Honest stdlib coverage: **~97%** of major standard library *areas*

### General-purpose Toolchain/IDE debug increment (Done)

- VS Code now contributes `mako-native` launch configs and a
  `Mako: Debug Active File` command.
- Debug launch builds the active `.mko` file first, then delegates the native
  binary to CodeLLDB (`lldb`) or Microsoft C/C++ (`cppdbg`).
- Extension settings now include `mako.debug.adapter`; packaged editor scaffold
  includes the matching build task and docs.
- Product intention advanced through the VS Code native debug milestone.

### General-purpose Toolchain/IDE dependency audit increment (Done)

- `mako pkg audit` now reads `mako.lock` and checks local `mako-cve.toml`
  advisory ranges plus `mako-license.toml` allow/deny license policy.
- Audits are fully offline and fit private registry / vendored policy workflows.
- The package-manager example now includes clean advisory and license policy
  files as a reproducible audit demo.
- Product intention advanced through the offline dependency audit milestone.

### General-purpose Toolchain/IDE documentation generator increment (Done)

- `mako doc` now writes API markdown plus `examples.md` runnable commands and a
  `search-index.json` symbol index.
- Generated docs include `mako check`, `mako run`, and `mako test` commands for
  files that contain runnable mains or test functions.
- Tests: `doc_generates_runnable_examples_and_search_index`
- Product intention advanced through the documentation generator milestone.

### General-purpose Toolchain/IDE testing-tools increment (Done)

- `mako test --coverage` now prints package source/test file coverage and
  category counts.
- Test discovery now includes `Fuzz*`, `Property*`, `Snapshot*`, `Mock*`, and
  `Fixture*` zero-arg functions alongside `Test*` / `test_*`.
- Tests: `tooling_quality_test.mko`
- Product intention: Toolchain/IDE track **97%**, overall **~78%** after correcting the weighted roadmap table against the updated backend/game vision.

### General-purpose Observability profile increment (Done)

- Added `mako profile [path] [-p NAME] [--release] [--json] -- [args...]`.
- Reports frontend, backend, build, run, total wall time, and exit status; JSON
  output uses the stable `mako.profile.v1` schema.
- Product intention: Observability/debugging track **48%**, overall remains
  **~78%** until full CPU/memory/allocation/scheduler/lock profiling lands.

### General-purpose Release packaging docs increment (Done)

- Release archives now copy the complete `docs/` tree, including the book,
  howtos, performance notes, roadmap, status, and internal planning docs, plus
  top-level README and changelog.
- Product intention: Installer/distribution track **77%**, overall remains
  **~78%**.

### General-purpose Data/SQL compile-time serialization increment (Done)

- `#[derive(json)]` now generates compile-time serializers for arbitrary
  supported scalar field counts instead of falling back to placeholder JSON
  after the first narrow shapes.
- Generated `*_to_json` and `*_<field>_from_json` helpers use direct JSON
  builtins and do not require runtime reflection lookup for marshaling.
- Tests: `derive_json_codegen_test.mko`
- Product intention advanced through the compile-time serialization milestone.

### General-purpose Data/SQL multi-store compatibility increment (Done)

- Added dependency-free compatibility helpers for MongoDB, Cassandra,
  ClickHouse, and Elasticsearch URL/request construction.
- New builtins: `mongo_connect_url`, `mongo_find_one_request`,
  `cassandra_connect_url`, `cassandra_select`, `clickhouse_connect_url`,
  `clickhouse_select`, `elastic_connect_url`, `elastic_search_request`.
- Tests: `multistore_compat_test.mko`
- Product intention advanced through the multi-store compatibility milestone.

### General-purpose Data/SQL MySQL/Redis polish increment (Done)

- Redis now has URL parsing, reusable connection handles, connection status,
  close, and `redis_conn_*` command helpers.
- MySQL/MariaDB now has DSN validation, driver detection, parsed connection
  metadata, and status/close helpers.
- Tests: `mysql_redis_polish_test.mko`
- Product intention advanced through the MySQL/Redis polish milestone.

### General-purpose Data/SQL typed-checker increment (Done)

- Unified SQL now has `sql_check_typed(schema, sql, params, result)` for
  static table, column, placeholder, nullability, and result-shape checks.
- The checker supports simple SELECT and INSERT shapes without opening a
  database connection.
- Tests: `sql_typed_check_test.mko`
- Product intention advanced through the typed SQL checker milestone.

### General-purpose Data/SQL migration increment (Done)

- Unified SQL now has numeric-version migrations:
  `sql_migration_applied` / `sql_migrate`.
- Migrations use `mako_schema_migrations`, transaction boundaries, and
  parameterized version tracking through the existing unified SQL facade.
- Tests: `sql_migration_test.mko`
- Product intention advanced through the Data/SQL migration milestone.

### General-purpose Data/SQL transaction increment (Done)

- Unified SQL now has transaction helpers:
  `sql_begin` / `sql_commit` / `sql_rollback`.
- Unified prepared statements now work across existing drivers:
  `sql_prepare` / `sql_stmt_query_int` / `sql_stmt_exec` / `sql_stmt_close`.
- SQLite uses real `sqlite3_stmt` handles when linked; Postgres uses named
  libpq prepared statements when connected.
- Tests: `sql_tx_stmt_test.mko`
- Product intention advanced through the Data/SQL transaction/prepared-statement milestone.

### General-purpose Data/SQL increment (Done)

- Unified SQL now has bounded lazy connection pools:
  `sql_pool_open_sqlite` / `sql_pool_open_postgres` / `sql_pool_query_int` /
  `sql_pool_exec` / `sql_pool_close` plus pool status/metric helpers.
- SQLite `SqlDB` handles now keep a reusable connection when libsqlite is linked,
  so pool slots are real backend handles instead of DSN-only facades.
- Tests: `sql_pool_test.mko`
- Product intention advanced through the Data/SQL pooling milestone.

### Stdlib Wave 7 (Done)

- RE2 `\xHH` / `\n` / `(?:…)`; GIF LZW dictionary decode; JPEG Huffman-block APP9
- html/template nested; gob `[]string`; smtp AUTH PLAIN + STARTTLS probe
- reflect clone/equal
- Tests: `goext_wave7_test.mko`
- Suite: **90 passed**, 0 failed
- Honest stdlib coverage: **~95%** of major standard library *areas* (not full symbol parity)

### `mako version` — Done

- `mako version` → `mako version mako0.1.0 darwin/arm64` (Cargo.toml + os/arch)
- `mako --version` / `-V` aligned; `mako version -v` optional commit (`MAKO_GIT_HASH` / git)
- Docs: README · GUIDE · howto/01

### Grouped imports — Done

- `import ( "a" \n "b" )` · brace `import { "a"; "b" }` · `alias "path"` · `"path" as x`
- `mako fmt` emits `import ( … )` for 2+ imports
- Tests: `import_group_test.mko` · `import_brace_test.mko`

### Stdlib Wave 6 (Done)

- binary BE; html/template `range`/`with`; gob struct bag; GIF LZW decode
- smtp dialog soft; reflect field_at/schema; regexp `\Q`/`\G`
- Tests: `goext_wave6_test.mko`
- Suite: **89 passed**, 0 failed
- Honest stdlib coverage: **~93%** of major standard library *areas* (not full symbol parity)

### Stdlib Wave 5 (Done)

- Multi-file zip writer/list; RE2-ish `{n,m}` `\b` `\A` `\z`; html/template `if`/multi-key
- gob `map[string]string`; encoding/binary LE; net/smtp format + soft dial
- reflect value bag; JPEG DCT DC marker path; GIF LZW encode
- Tests: `goext_wave5_test.mko`
- Suite: **86 passed**, 0 failed
- Honest stdlib coverage: **~91%** of major standard library *areas* (not full symbol parity)

### Operators (Done)

- Comparison: `==` `!=` `<` `>` `<=` `>=` (`=` remains assignment only)
- Logical: `&&` `||` `!` (+ `and`/`or`/`not`); **short-circuit** codegen for `&&`/`||`
- Bitwise: `&` `|` `^` `&^` `<<` `>>`; unary `^` (complement); `!!x` = two `!`
- Docs: KEYWORDS / GUIDE §2c / LANGUAGE · Tests: `operators_go_test.mko`
- Suite: **85 passed**, 0 failed

### Stdlib Wave 4 (Done)

- zip **deflate** (zlib raw) · GIF/JPEG roundtrip seeds · reflect struct schema
- html/template (auto-escape) · encoding/gob · net/mail · log/slog
- regexp: `[:digit:]`/escapes in classes · `regex_valid` / `regex_quote_meta`
- Tests: `goext_wave4_test.mko`
- Honest stdlib coverage: **~86%** of major standard library *areas* (not full symbol parity)

### Stdlib Wave 3 (Done — raised area coverage)

- `runtime/mako_goext.h`: archive/zip (store), image/png, maps helpers, reflect (minimal),
  testing/httptest, AES-GCM + ChaCha20-Poly1305 (OpenSSL), mime/multipart, recursive
  `filepath_walk` / `filepath_walk_n`, regexp `find_all` / `replace` / `replace_all`
- `runtime/mako_rt.h`: RE2-ish `\d` `\D` `\w` `\W` `\s` `\S` escapes
- `std/`: archive/zip, image/png, maps, reflect, testing/httptest, mime/multipart + crypto/regexp updates
- Tests: `examples/testing/goext_wave3_test.mko`
- Suite: **83 passed**, 0 failed
- Honest stdlib coverage: **~72%** of major standard library *areas* for backends (not full symbol parity)
- Remaining gaps: full RE2, zip deflate, JPEG/GIF, deep reflect, gob/mail/slog, …

### Stdlib Waves 1–2 (Done — Partials noted)

- `runtime/mako_goext.h`: flag, exec, url, csv/xml, gzip (+ zlib auto-link), tar, mime,
  context deadlines, bytes.Buffer, rand, template, html, base32, sha1/sha512, DNS/IP,
  signal, atomic, utf8, filepath_walk, slices, embed_file
- `std/` wrappers for new packages; zlib via `find_zlib` / `-DMAKO_HAS_ZLIB`
- Tests: `examples/testing/goext_wave_test.mko`
- Suite: **82 passed**, 0 failed
- Honest stdlib coverage: **~55%** of target standard library areas for backends (not full parity)
- Partials (closed in Wave 3): zip, image, reflect, httptest, maps helpers, AEAD ciphers

### Stdlib polish (Done)

- **RWMutex:** `rwmutex_new` / `rlock` / `runlock` / `lock` / `unlock`
  (`runtime/mako_stdlib.h`) · `examples/testing/rwmutex_test.mko`
- **`import "strings"`** (and `path`, `fmt`, `sync`, `bufio`, `math`, `os`,
  `time`, `crypto`, `log`, `strconv`, `collections`, `errors`, `regexp`,
  `encoding/{json,hex,base64}`, `database/sql`, `net/http`): resolve under
  `std/`, auto-alias to package basename; `MAKO_STD` override
  (`src/tooling.rs`) · `examples/testing/std_import_test.mko`
- Suite: **81 passed**, 0 failed

### Stdlib Partials closed (Done)

- **bufio:** `buf_reader_new` / `from_string` / `read_line` / `read` / `buf_writer_*`
  (`runtime/mako_stdlib.h`) · `examples/testing/bufio_test.mko`
- **Typed `HttpRequest`:** `http_request_parse` / `from_conn` / accessors
  (`runtime/mako_http.h`) · `http_request_type_test.mko` · `examples/http_lib/request_type.mko`
- **Unified `database/sql`:** `sql_open_sqlite` / `sql_open_postgres` / `sql_query_int` /
  `sql_exec` / `sql_ok` / `sql_close` (`runtime/mako_db.h`) · `sql_unify_test.mko`
- **SQL transactions/statements:** `sql_begin` / `commit` / `rollback` /
  `sql_prepare` / `sql_stmt_*` (`runtime/mako_db.h`) · `sql_tx_stmt_test.mko`
- **SQL migrations:** `sql_migration_applied` / `sql_migrate`
  (`runtime/mako_db.h`) · `sql_migration_test.mko`
- **Typed SQL checker:** `sql_check_typed` for table/column/param/nullability/
  result shape (`runtime/mako_db.h`) · `sql_typed_check_test.mko`
- **MySQL/MariaDB + Redis polish:** MySQL/MariaDB DSN validation + Redis URL
  parsing/reusable connection helpers (`runtime/mako_db.h`) ·
  `mysql_redis_polish_test.mko`
- **Multi-store compatibility:** MongoDB, Cassandra, ClickHouse, and
  Elasticsearch URL/request helpers (`runtime/mako_db.h`) ·
  `multistore_compat_test.mko`
- **Compile-time JSON derive:** generated scalar field serializers/extractors
  without runtime reflection lookup (`src/desugar.rs`) ·
  `derive_json_codegen_test.mko`
- **SQL pools:** `sql_pool_open_*` / `query_int` / `exec` / metrics / close
  (`runtime/mako_db.h`) · `sql_pool_test.mko`
- Docs: STDLIB / ROADMAP / STATUS / howto HTTP synced
- WaitGroup: `wait_group_new` / `add` / `done` / `wait`
- Suite: **79 passed**, 0 failed

### Standard library expansion (Done)

- New `runtime/mako_stdlib.h`: strings (split/join/trim/replace/…), strconv/fmt,
  path/fs/os (`path_clean`, `getcwd`, `read_dir`, …), math, collections, time
  RFC3339, hex_encode, random_bytes, mutex, log_debug/log_kv
- Builtins wired in `src/types/mod.rs` + `src/codegen/mod.rs`; `-lm` on Unix
- Tests: `examples/testing/stdlib_strings_test.mko`, `stdlib_path_math_test.mko`
- Demo: `examples/stdlib/demo.mko`
- `mako test --race` / `--sanitize` plumbed through `cmd_test` → clang

### Security / safety language (Done)

- Move checker: clearer use-after-move diagnostics (CFG NLL)
- `secret_from_str` / `secret_drop` + `mako_secure_zero` (zero-on-drop)
- `unsafe { }` + `unsafe_index` — explicit rare bounds opt-out
- HTTP header validation (`http_header_ok`, reply Content-Type)
- Parameterized SQLite/Postgres (`sqlite_query_*_params`, `pg_exec_params`)
- `const_eq` / `crypto_eq` constant-time compare
- Crew exit `cancel_join` — tasks cannot outlive cancel policy
- `[package] systems = true` — GC never weakens ownership
- Tests: `security_test`, `cancel_policy_test`, `db_params_test`
- Docs: [SECURITY.md](docs/SECURITY.md)

### HTTP library + how-tos (Done)

- Client: `http_get` / `http_post` / `http_request` / timeouts / `http_last_status` / `http_last_header`
- Server reason phrases; STDLIB HTTP section; `examples/http_lib/` + `scripts/http-lib-smoke.sh`
- `docs/howto/` (getting started, HTTP, errors, packages, concurrency, memory, WASI, testing, release)
- Syntax docs sync: KEYWORDS 38, labeled break in GUIDE, LANGUAGE release `-O3`

### Memory & CPU efficiency (Done)

- `now_ns` / `black_box`; three-way ns benches (`scripts/bench-vs-go-rust.sh`)
- Map pre-size + move rehash; slice make zeros only `len`; fast-path append
- HTTP `arena_cstr` / `arena_text_n` (no malloc→arena double copy)
- Release `-DNDEBUG` elides bounds checks (debug still aborts) — [PERFORMANCE.md](docs/PERFORMANCE.md)
- Measured: fast on fib/map benchmarks; optimized map operations; see PERFORMANCE.md table

### Incremental builds + native objects (Done)

- `.mako/cache/` keyed by source hash + compiler version + flags; per-unit `.o` + link
- Parallel `-j` / `MAKO_JOBS`; `--no-incremental` escape
- Incremental typecheck with full-program fingerprint (NLL never skipped on stale)
- Memory-safe cache I/O — [BUILD.md](docs/BUILD.md) · [SECURITY.md](docs/SECURITY.md)

### Performance (Done)

- Release: clang `-O3 -flto -DNDEBUG`; optional `MAKO_STRIP`
- Map rehash moves keys; benches `examples/bench/` + `scripts/bench-vs-go.sh`
- [PERFORMANCE.md](docs/PERFORMANCE.md)

### Backend / systems / API / DB engines (Done)

- `http_respond_json`, `append_file`
- `examples/api_backend/`, `systems_log/`, `db_engine/` + tests
- `mako init --backend`; GUIDE: Building APIs / Systems / DB engines

### Package manager (Done)

- `mako pkg install` / `update` / `lock` / `publish` — SemVer resolve, `mako.lock`, local registry
- Module `src/pkg.rs`; example `examples/pkg_manager/`

### Errors & debugging (Done)

- `errorf` / `error_is` / `error_string` + existing `error` / `wrap_err` / `?`
- `dbg` / `dbg_str`; abort hints → `docs/DEBUG.md`; assert_eq got/want wording
- Default builds already use clang `-g`

### Labeled loops (Done)

- `label: while` / `label: for` + `break label` / `continue label`

### CFG NLL (Done)

- `src/types/nll.rs`: const-bool edge prune, diverge helpers, loop re-entry detection
- Loop fixpoint only when body can reach header again (fixes always-`break` false positives)
- Nested always-break / `if false` / `while false` examples; `hold_const_true_after` bad case
- GUIDE + LANGUAGE updated — residuals: no labeled break/continue; share ≠ full RC

### Packaging

- SemVer (`^` / `~` / exact) for path deps; local registry `.mako/registry/<name>/<ver>/`
- `mako pkg list` resolves registry-only deps; example `examples/pkg_registry/`

### Servers (beachhead Done)

- H2 multi-accept `tls_serve_h2_routes`; gRPC unary+stream live smoke script
- HTTP/HTTPS/H2/gRPC/H3-client seeds as documented in STATUS

### Product / packaging (earlier)


- `mako --version` from Cargo.toml; polished `--help`
- `mako init [path] [--name]` → `mako.toml` + `main.mko`
- `mako pkg list` — path + git deps; git shows `[fetched]` / `MISSING — run pkg fetch`
- `mako pkg fetch` — clone git deps into `.mako/deps/` (needs git + network; not default CI)
- Build/check merge fetched git trees like path deps; clear MISSING if not fetched
- `mako init [path] --workspace` → root `[workspace] members` + `lib/` + `app/` (path dep); default init unchanged
- Local workspace sketch: check/build/test/run/fmt/lint/bench + `-p`
- Example: `examples/pkg_path_dep/`
- Help honesty: `doc` / `deploy docker` described as stubs
- GUIDE + STDLIB: package / workspace / git-fetch workflow
- Homebrew formula sketch `Formula/mako.rb`
- Release checklist `docs/RELEASE.md`
- CI: cargo build + `mako test examples/testing` (no live network deps)

### Language / runtime (already in tree)

- Compiler pipeline `.mko` → C → native; `crew` / actors / arenas / Result
- `mako test`; tooling: fmt / lint / bench / doc / lsp / pkg
- OpenSSL / nghttp2 / quiche client seeds (opt-in)
- **WASI preview1:** `mako build --target wasm32-wasi` uses wasi-sdk clang
  (`wasm32-wasip1`), `-DMAKO_WASI` minimal runtime; `examples/wasi_hello.mko`,
  `wasi_args_env.mko`, `wasi_fs.mko`; `scripts/wasi-verify.sh`
- **HTTP/1.1 / HTTPS / H2 servers** + smokes (skip without OpenSSL)

### External blocker

- Not published to **homebrew-core** (needs maintainer tap/org + PR) — see STATUS

### Out of STATUS 100% bar (VISION Later)

- SIMD / GPU / optional GC / full LSP / production gRPC framework / in-process H3 server
- WASI sockets / preview2 / browser DOM
