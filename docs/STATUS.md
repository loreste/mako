# Mako status (adversarial / verified)

Last inventory: 2026-07-11 (**unique Mako surface** · pack/pull · pain map · suite **130+** · **The Mako Book**).

**Book:** [The Mako Book](book/) · **Guide:** [GUIDE.md](GUIDE.md) · **Identity:** [IDENTITY.md](IDENTITY.md) · **Pain points:** [PAIN_POINTS.md](PAIN_POINTS.md) · **Build:** [BUILD.md](BUILD.md) · **Stdlib:** [STDLIB.md](STDLIB.md) · **Roadmap:** [ROADMAP.md](ROADMAP.md) · **Changelog:** [../CHANGELOG.md](../CHANGELOG.md).

---

## Completion estimate (honest)

| Scope | Approx. |
|-------|---------|
| **MVP / usable language** | **100%** |
| **STATUS north-star** | **100%** |
| **Mako identity (preferred syntax)** | **~90%** — [IDENTITY.md](IDENTITY.md) |
| **Go/Rust pain coverage** | **~80%** strong rows — [PAIN_POINTS.md](PAIN_POINTS.md) |
| **Dual-form coverage (optional sugar)** | **~78%** — [GO_SYNTAX_CHECKLIST.md](GO_SYNTAX_CHECKLIST.md) |
| **Standard library** | **~98%** of target areas Done (Wave 9; not every symbol) |

---

## Docs — **Done**

| Piece | Status |
|-------|--------|
| **The Mako Book** (`docs/book/` · mdBook `book.toml` + chapters) | Done |
| Accuracy pass: README / GUIDE / STATUS / ROADMAP / howto index | Done |

## Tooling — **Done**

| Piece | Status |
|-------|--------|
| `mako version` / `--version` with OS/arch | Done |
| Grouped `import (` / `{` + fmt | Done |
| Packs & pulls (`pack`/`pull` flair, always qualify, `import`/`package` dual, internal rewrite) | Done |
| Low-ceremony ergonomics doc + tests (`print` poly, `==` strings, match routes) | Done |
| Path-style import blocks (nested std, vendor/, module=, aliases, blank-line groups) | Done |
| Speed / concurrency / parallelism north star ([SPEED.md](SPEED.md)) | Done (product bar) |
| `fan` + Mako `fn` lambdas (block body codegen + types) · crew/fan tests | Done |
| CLI help polish (`build`/`run`/`check`/`test` flag docs; `version` near top) | Done |
| VS Code `mako-native` launch configs through LLDB/cpptools | Done |
| `mako pkg audit` offline advisory and license policy checks | Done |
| `mako doc` API markdown, runnable examples, and search index | Done |
| `mako test --coverage` plus fuzz/property/snapshot/mock/fixture categories | Done |
| `mako profile` wall-clock compile/run profile reports with JSON output | Done |
| Release archives include the full internal docs tree and top-level release notes | Done |

---

## Standard library — Wave 9

| Area | Status |
|------|--------|
| RE2 backrefs `\1`–`\9` · `\p{L/N}` ASCII · `[:lower:]`/`[:upper:]`/`[:punct:]` | Done |
| JFIF grayscale encode (`jpeg_encode_gray_jfig` + `jpeg_is_jfif`) | Done |
| Reflect type schema registry from codegen constructors | Done |
| SMTP STARTTLS soft path + AUTH PLAIN; OpenSSL probe | Done |
| `str_cut` / `str_count` | Done |
| UTF-8-aware regexp `\p{...}` for common scripts/categories + simple lookahead | Done |
| Tests | `goext_wave8_test.mko`, `goext_wave9_test.mko` |

---

## Verified this session

| Check | Result |
|-------|--------|
| `cargo build --release` | PASS (prior) |
| Book samples `mako check` / `run` | PASS — `docs/book/examples/book_*.mko` |
| `mako test examples/testing` | PASS — **165 passed**, 0 failed |
| `if init; cond { }` + both-branches-return body | Done — `examples/testing/if_init_test.mko` |
| Go `switch`/`case`/`default` (value, expr-less, init) | Done — `examples/testing/switch_test.mko` |
| Positional struct literals `Point{1, 2}` / `Point{}` | Done — `examples/testing/struct_positional_test.mko` |
| `go f()` → kick onto enclosing crew | Done — `examples/testing/go_stmt_test.mko` |
| Compound assign `+= … ++ --` (ident/field/index) | Done — `examples/testing/compound_assign_test.mko` |
| Go `for` forms (C-style, while, infinite, range) | Done — `examples/testing/for_forms_test.mko` |
| Parallel binding/assignment (`a, b = b, a` swap) | Done — `examples/testing/parallel_assign_test.mko` |
| `if` as an expression (`let x = if c { a } else { b }`) | Done — `examples/testing/if_expr_test.mko` |
| Argon2id password hashing (`crypto.password_hash`) | Done (OpenSSL) — `examples/testing/password_hash_test.mko` |
| UDP proxy routing (`game_udp_sender_addr` / `send_to`) | Done — `examples/testing/udp_proxy_test.mko` |
| Sendable sync handles across kick (CMap/Mutex/RWMutex/AtomicInt) | Done — `examples/testing/kick_sync_test.mko` |
| PBKDF2-HMAC-SHA256 (`crypto.pbkdf2`, SCRAM primitive) | Done — `examples/testing/pbkdf2_test.mko` |
| Per-connection HTTP/2 state (`http2_conn_new`/`use`/`free`) | Done — `examples/testing/http2_multiconn_test.mko` |
| HTTP/2 read request + `http2_response` (full request/response cycle) | Done — fixed inverted stream parity; `examples/testing/http2_request_test.mko` |
| HPACK decode for real clients (Huffman, indexed names, full static table) | Done — curl `--http2` verified; `examples/testing/hpack_decode_test.mko` · `examples/h2_dynamic_server.mko` |
| HTTP/2 reverse proxy (`http_forward` upstream + relay) | Done — curl→proxy→backend verified; `examples/h2_reverse_proxy.mko` |
| TCP pool + `http_forward_full` + `http_proxy_raw` | Done — pool reuse, status/body, raw pump; `examples/testing/proxy_pool_test.mko` |
| HTTP parse object + chunked decode | Done — `http_parse` / `http_decode_chunked` |
| Nonblocking connect + fd splice/copy | Done — `tcp_connect_nb` / `tcp_fd_copy` / `tcp_splice` |
| Socket tuning (`reuseport`, buffers, `accept4`) | Done |
| Async TLS accept (`tls_accept_start` / handshake step) | Done — event-loop friendly surface |
| HTTP/2 stream multiplexing (ready queue, 32 slots) | Done — `http2_next_ready_stream` / `stream_take` / `stream_body` |
| HTTP/3 server surface (UDP bind/poll/stream) | Done — `h3_server_*` (quiche when linked) |
| Proxy edge cases (headers, chunked, 204/304, pool release latency) | Done — `examples/testing/proxy_edge_test.mko`; docs in BUILTINS *Reverse-proxy notes* |
| Checked integer overflow (`--overflow trap`, `checked_*`, `would_overflow_{add,sub,mul}`) | Done — full wire types+codegen+runtime; `overflow_shutdown_test.mko` |
| Parser multi-error recovery | Done — `parse_with_errors` keeps following good decls (unit test); `examples/bad/multi_error.mko` |
| Graceful shutdown builtins | Done — `signal_on_term` / `server_drain` / `register_listener` |
| Leak scopes | Done — `leak_scope_enter` / `exit` / `leak_check` |
| Trace id / spans | Done — `trace_id` / `begin` / `end` / `log` |
| `mako dev` hot reload seed | Done — mtime poll rebuild+rerun |
| `Result[int, Enum]` typed errors + match | Done — `result_enum_test.mko` |
| `const fn` compile-time fold | Done — `const_fn_test.mko` |
| `crew.drain` + `evloop_shutdown` | Done — `crew_drain_test.mko` |
| NLL const-fold comparisons | Done — more int/ident folds for dead edges |
| Module layout (suggested order) | Done — `src/overflow.rs`, `recovery.rs`, `shutdown.rs`, `errors.rs`, `leak.rs` + runtime headers |
| bcrypt (`$2b$`) via libxcrypt (`crypto.bcrypt`/`bcrypt_check`) | Done — verified on Linux x86_64: round-trip + distinct salts; `examples/testing/bcrypt_test.mko` |
| SCRAM-SHA-256 core (`crypto.scram_*`, raw `sha256`/`hmac`, `xor_bytes`) | Done — RFC 7677 vector byte-exact on Linux; `examples/testing/scram_test.mko` |
| Native bind-address control (`tcp_listen_addr`) | Done — verified on Linux: loopback-only bind, non-host IP rejected |
| TLS 1.3 termination | Verified on Linux — `openssl s_client -tls1_3` → `TLSv1.3` / `TLS_AES_256_GCM_SHA384` |
| Socket-style TLS server (`tls_server_new`/`tls_accept`/`read`/`write`/`alpn`) | Done — STARTTLS-upgrade verified; `examples/testing/tls_server_test.mko` |
| Signal hooks by name (`signal_watch`/`fired`/`ignore` HUP/TERM/…) | Done — reload/shutdown verified; `examples/testing/signal_test.mko` |
| File-system watch (`watch_new`/`add`/`poll`/`close`, kqueue+inotify) | Done — change detection verified; `examples/testing/watch_test.mko` |
| Contextual `pack`/`pull`/`switch`/`go` (usable as identifiers) | Fixed — no longer reserved words |
| C keyword / stdlib-name identifiers (`switch`, `read`, `time`, …) emit valid C | Fixed — codegen mangles reserved & libc names |
| `mako fmt` doubled `export` on structs | Fixed |
| The Mako Book + docs accuracy | Done |

---

## Wave 10 — language core (compat-safe)

| Piece | Status |
|-------|--------|
| User generics monomorphization `fn id[T](x: T) -> T` | Done |
| Mako methods `on Type { fn m(self) … }` | Done (desugars to `Type_m`) |
| Tuples + tuple patterns | Done |
| `export` + `visibility = "explicit"` seed | Done (default open) |
| `chan_open[T]` / `make(chan[T], n)` (int + string) | Done; `chan_new` unchanged |
| `#line` source maps + `bounds_checks = "on"` profile | Done |
| Compat policy | [COMPAT.md](COMPAT.md) |
| Tests | `examples/testing/lang_wave10_test.mko` (6 tests) |

## Wave 10b — Go-first surface

| Piece | Status |
|-------|--------|
| `func` alias for `fn` | Done |
| `var` / `:=` short declaration | Done |
| Go method receivers `func (p Point) m() int` | Done |
| `type Point struct { x int }` (no colon) | Done |
| Params `a int` / `a, b int` (no colon) | Done |
| Bare returns `func f() int` | Done |
| `package main` clause | Done |
| Multi-return `a, b := f()` | Done |
| Capitalized export (Go-style) | Done |
| Checklist with % | [GO_SYNTAX_CHECKLIST.md](GO_SYNTAX_CHECKLIST.md) **~78%** |
| Example / tests | `examples/go_style.mko`, `go_style_test.mko` (6 tests) |

## True hard residuals

**Closed this pass (gap close):**

| Piece | Status |
|-------|--------|
| Send-like kick rules (Copy / string / chan only) | Done — tests `kick_send_test`, bad `kick_array_arg` |
| `visibility = "explicit"` filters pulled symbols | Done — `examples/export_vis/` |
| `Ok`/`Err` respect enclosing `Result[T, E]` | Done — `errors_typed_test` |
| `chan_open` int family + bool | Done |
| `fan` uses HW concurrency (not fixed 4 threads) | Done |
| `fan` on `[]float` (`mako_par_map_float`) | Done |
| ShareInt/Arena rejected across kick | Done |
| `scripts/bench-gate.sh` vs Rust (fib/slice/map, default ≤2.5×) | Done |
| `chan_open[string]` + kick with chan handle | Done |
| `error_context` (wrap_err alias) | Done |
| `mako lint --identity` (dual spellings as style) | Done |
| Atomic `share` RC + `share_set` | Done |
| `fan` on `[]string` (`mako_par_map_str`) | Done |
| `error_join` combine Results | Done |
| bench-gate default ≤2.0× Rust (fib/slice/map) | Done |
| ShareInt + string kick auto-clone heap pack | Done |
| bench-gate strict 1.5× (`MAKO_BENCH_STRICT=1` or arg) | Done (passes locally) |
| `chan_open[Struct]` via MakoChanPtr heap-box | Done |
| `error_tag(tag, msg)` enum-like string errors | Done |

**Closed (wave 7–18):** join_timeout **flatten** · POD kick + string fields ·
`reflect_value_of` N + nested POD · `Result`/`Option` Ok for scalars + containers
+ **generic Option[T]** + **Result[Option[T]]** nest · NLL labeled continue/break ·
script `\p{…}` (Syriac/Coptic/Runic) · expanded TSan · prior work.

**Wave 18 tests:** `examples/testing/wave18_queue_test.mko`.

**Pain residuals (language) still open:** see [PAIN_POINTS.md](PAIN_POINTS.md) §4.

1. Fuller data-race model beyond expanded TSan smoke (no full type-level race system)  
2. More Result/Option shapes (deeper nesting, Option of containers mono polish)  
3. Stronger NLL (rarer multi-label CFG products)  

**Stdlib / product residuals:**

6. Complete Unicode property database / full PCRE (script seed set growing)  
7. Huffman JPEG bitstream readable by arbitrary viewers (JFIF shell + APP7 today)  
8. Reflect for non-POD fields (maps/slices); nested POD done  
9. Symbol-level parity inside Done packages

---

## External

homebrew-core publish — [Formula/mako.rb](../Formula/mako.rb).
