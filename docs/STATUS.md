# Mako status (adversarial / verified)

Last inventory: 2026-07-14 (**unique Mako surface** · pack/pull types · full map/slice/bag *language* surface with **demand-driven monomorphs** · suite **130+** · **The Mako Book**).

**Book:** [The Mako Book](book/) · **Guide:** [GUIDE.md](GUIDE.md) · **Identity:** [IDENTITY.md](IDENTITY.md) · **Pain points:** [PAIN_POINTS.md](PAIN_POINTS.md) · **Build:** [BUILD.md](BUILD.md) · **Stdlib:** [STDLIB.md](STDLIB.md) · **Roadmap:** [ROADMAP.md](ROADMAP.md) · **Changelog:** [../CHANGELOG.md](../CHANGELOG.md).

---

## Completion estimate (honest)

| Scope | Approx. |
|-------|---------|
| **MVP / usable language** | **100%** |
| **STATUS north-star** | **100%** |
| **Mako identity (preferred syntax)** | **~100%** — [IDENTITY.md](IDENTITY.md) |
| **Go/Rust pain coverage** | **~80%** strong rows — [PAIN_POINTS.md](PAIN_POINTS.md) |
| **Dual-form coverage (optional sugar)** | **~90%** — [GO_SYNTAX_CHECKLIST.md](GO_SYNTAX_CHECKLIST.md) |
| **Standard library** | **~98%** of target areas Done (Wave 9; not every symbol) |

---

## Docs — **Done**

| Piece | Status |
|-------|--------|
| **The Mako Book** (`docs/book/` · mdBook `book.toml` + chapters) | Done |
| Accuracy pass: README / GUIDE / STATUS / ROADMAP / howto index | Done |
| Collections surface docs (ERGONOMICS · LANGUAGE · BUILTINS · book ch03/ch14/ch15 · howto/10 · llms*) | Done — full map/slice/bag surface + demand-driven monomorphs |

## Tooling — **Done**

| Piece | Status |
|-------|--------|
| `mako version` / `--version` with OS/arch | Done |
| Grouped `import (` / `{` + fmt | Done |
| Packs & pulls (`pack`/`pull` flair, always qualify, `import`/`package` dual, internal rewrite) | Done |
| Low-ceremony ergonomics doc + tests (`print` poly, `==` strings, match routes, maps/slices) | Done — [ERGONOMICS.md](ERGONOMICS.md) |
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
| `cargo build --release` | PASS |
| `map[K]Option[T]` / `map[K]Result[T,E]` | PASS — `map_option_result_test` (11 tests) |
| Security residuals (at-rest, limits, cancel, mTLS, SCRAM cbind) | PASS — `security_residuals_test` |
| Security product polish (path size, PEM, CSR/self-signed, prom/trace, SCRAM-PLUS helpers) | PASS — `security_product_test` |
| Backend ergonomics already on tip (`for…in range`, `fmt_sprintf*`, `match`/`switch`, `chan[Struct]`, POD kick) | Documented — [ERGONOMICS.md](ERGONOMICS.md) · [SPEED.md](SPEED.md) |
| Struct update `S { ..base, field: v }` + POD enum kick / `chan[Enum]` | Done — `struct_update_test` |
| First-class fns · `f"…"` · field defaults · `chan[tuple]` | Done — `lang_ergonomics_test` |
| Portable timeouts (`send/recv_timeout`, `join_deadline`, `deadline_remaining_ms`) | Done — `timeout_portable_test` |
| Crew child error prop (`first_err` / `wait`) · `detach` · actor state | Done — `crew_error_prop_test` · `detach_test` · `actor_test` |
| Observability depth (OTLP JSON, profile snapshot, stack_trace, crash_report, PGO/LTO) | Done seed — `observability_depth_test` |
| Capturing closures (POD + string + struct env via `MakoFn`) | Done seed — `capturing_closure_test` · `struct_capture_test` |
| Kick first-class `fn` values across crew (Send) | Done seed — `kick_fn_test` |
| f-string format specs (flags `+ # - 0`, hex/oct/bin, float e/f/g) | Done seed — `fstring_fmt_test` |
| `fn_drop` / env free + debugger/task inspect seeds | Done seed — `fn_drop_debug_test` |
| Storage page + WAL seeds | Done seed — `storage_wal_test` |
| Hash index + store txn + snap predict seeds | Done seed — `store_index_test` |
| Domain tracks (btree/LSM/MVCC/rollback/gfx/AI/debug frame) | Done seed — `domain_tracks_test` (no SIPREC/WebRTC) |
| Storage depth (btree disk, SST, pcache, mvcc_gc, simd) | Done seed — `storage_depth_test` |
| LSM compact · store WAL recover · hot-reload mtime | Done seed — `domain_tracks_test` (`lsm_compact`, `store_recover_wal`, `hot_reload_*`) |
| Multi-level LSM (L1–L3) · page-backed btree | Done seed — `domain_tracks_test` (`lsm_compact_down`, `pbtree_*`) |
| Storage polish (bloom · range · disk page manager) | Done seed — `domain_tracks_test` (`bloom_*`, `btree_range`/`sst_range`, `pman_*`) |
| SQL str4 empty-bind arity + multi-arg `sql_query_str2/3/4` | Done — `sql_str4_empty_bind_test` · `sql_query_str_multi_test` |
| Zero-copy string regions (language) | Done — `str_slice_eq` / `str_slice_index` / `str_at_eq` / `str_byte_at` · `str_slice_zc_test` |
| Debugger depth (line BP · frames · async parent · snapshot) | Done seed — `fn_drop_debug_test` |
| OTLP protobuf + HTTP exporter client | Done seed — `trace_export_otlp_pb` · `otlp_export_traces_*` · `observability_depth_test` |
| Sampling CPU profiler | Done seed — `profile_sample_*` · `profile_sample_test` |
| DAP JSON + pprof-text + tid samples | Done seed — `residual_seeds_test` · `profile_sample_test` |
| DAP handle + `mako dap` CLI · profile HTTP routes | Done seed — `dap_handle_request` · `/debug/pprof/*` |
| Cross-target FreeBSD/RISC-V dry-run | Done seed — `scripts/cross-target-seed.sh` · CI workflow |
| Comptime const if / comparisons | Done seed — `const_fn_test` (`abs_const` / `clamp_const`) |
| Hot-reload swap/stamp · predict service | Done seed — `residual_seeds_test` |
| DAP --stdio · profile-serve · plugin live reload · soft FB | Done seed — CLI + `residual_seeds_test` |
| MSI/notarize/brew/winget publish seeds | Done seed — scripts + `package-seed.yml` · WiX skeleton |
| gfx_poll · GPU backend stubs · netcode deltas | Done seed — `residual_seeds_test` |
| plugin_open/call/close · hot_reload_unwatch | Done seed — `residual_seeds_test` · `domain_tracks_test` |
| Installer UX (manifest + doctor + Windows) | Done seed — `install-manifest.json` · doctor fields · `install.ps1` |
| Actor spawn_cap + interface `on T : I` sugar | Done seed — `actor_test` · `iface_on_iface_test` |
| Error chain peel + tag helpers | Done seed — `error_unwrap` / `root` / `as_tag` / `has_tag` · `error_chain_test` |
| `fallthrough` switch dual | Done seed — `fallthrough_test` |
| Richer errors beyond stringly defaults | Done seed — `Result[T, Enum]` · wrap chain · `std/errors` |
| SIP library (platform builtins + `std/sip`) | Done — RFC 3261/3581 Via/RR/NAT, Digest HA1, framing, SDP rewrite |
| SIP zero-copy header/method views | Done — `sip_header_view` / `sip_method_eq` / `sip_header_eq` |
| ShareInt capture (shared mut via RC handle) | Done seed — `share_capture_test` |
| Packaging seeds (deb/rpm/winget/matrix/homebrew) | Done seed — scripts + packaging/ |
| Book samples `mako check` / `run` | PASS — `docs/book/examples/book_*.mko` |
| `mako test examples/testing` | PASS — **165 passed**, 0 failed |
| `if init; cond { }` + both-branches-return body | Done — `examples/testing/if_init_test.mko` |
| Go `switch`/`case`/`default` (value, expr-less, init) | Done — `examples/testing/switch_test.mko` |
| Go `fallthrough` (case body merge) | Done seed — `examples/testing/fallthrough_test.mko` |
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
| HTTP/2 stream multiplexing (ready queue, 64 slots) | Done — `http2_next_ready_stream` / `stream_take` / `stream_body` |
| HTTP/2 hardened path (dual FC, SETTINGS, auto WU, PADDED, overflow hard-fail) | Done — `http2_conn_*` · `examples/testing/http2_prod_test.mko` |
| HTTP/2 TLS one-shot (`tls_serve_h2_routes`) | Demo/smoke only — production path is `tls_server_new` + `http2_conn_*` |
| HTTP/3 server surface (UDP bind/poll/stream) | Done — `h3_server_*` (quiche when linked) |
| HTTP/3 hardened path (64 KiB bodies, no silent truncate, accessors, `h3_response`) | Done — `examples/h3_server.mko` · `h3_server_test.mko` · smoke |
| FS / storage production (`atomic_write`, `mkdir_all`, `remove_all`, dio CLOEXEC, mmap) | Done — `examples/testing/fs_storage_test.mko` |
| Low-level networking (peer/local addr, UDP sender, write_all/read_n, shutdown, CLOEXEC) | Done — `examples/testing/net_lowlevel_test.mko` |
| IPv6 dual-stack listen/connect + Happy Eyeballs `tcp_connect` | Done — `examples/testing/net_ipv6_he_test.mko` |
| Low-latency clocks (`mono_*` / deadlines / sleep_ns / spin_until) | Done — `examples/testing/time_latency_test.mko` |
| LLM programming (chat/tools/SSE/JSON extract, OpenAI-compatible HTTPS) | Done — `examples/testing/llm_test.mko` · `examples/llm_chat.mko` |
| LLM stream transport + embeddings + error/retry helpers | Done — `llm_chat_stream` / `llm_embed*` / `llm_is_error` / `llm_chat_retry` |
| SQL string params + last_insert_id / rows_affected (SQLite + Postgres) | Done — `examples/testing/sql_programming_test.mko` |
| SQL multi-row cursor + bulk first-column (`sql_query_rows*`, `sql_query_col_*`) | Done — `examples/testing/sql_rows_test.mko` |
| SIP/SDP/RTP platform (parse/build; build stacks in Mako — not a softswitch) | Done — `examples/testing/sip_test.mko` · `examples/sip_ua.mko` · `std/sip` |
| SRTP crypto building blocks (`aes_ctr`, `hmac_sha1` / `hmac_sha1_raw`) | Done — `examples/testing/crypto_srtp_blocks_test.mko` (HMAC RFC 2202) |
| TLS client socket API (`tls_client_new` / `tls_connect` + SNI/VERIFY_PEER) | Done — `examples/testing/security_crypto_test.mko` |
| Secrets helpers (`secret_len` / `secret_eq_str`) + HKDF-SHA256 | Done — RFC 5869 A.1 vector; `security_crypto_test.mko` |
| Strong structured logging (JSON/logfmt, levels, multi-field, file, redaction) | Done — `examples/testing/strong_log_test.mko` · `runtime/mako_log.h` |
| WebSocket RFC 6455 (client/server frames, mask, frag, ping/pong, close codes) | Done — `runtime/mako_ws.h` · `examples/testing/ws_api_test.mko` |
| GPU AI seed (OpenCL multi-vendor + host; matmul/relu/bias/softmax f32) | Done — `runtime/mako_gpu.h` · `gpu_seed_test.mko` (NVIDIA/AMD/Intel/Apple) |
| Local models (safetensors load, .makomodel, author MLP, linear HF layout) | Done — `runtime/mako_model.h` · `model_weights_test.mko` · `examples/model_mlp.mko` |
| GGUF F32/F16 load + attention/LN/GELU/SiLU + vocab tokenizer | Done — `model_load_gguf`, `gpu_attention_f32`, `tok_*` · `ai_depth_test.mko` |
| Multi-head attention + GGUF Q4_0/Q8_0 dequant + BPE tokenizer | Done — `gpu_mha_f32`, quant GGUF, `tok_encode_bpe` · `ai_depth_test.mko` |
| Email / SMTP (MIME builder, session, STARTTLS, AUTH PLAIN, mock e2e) | Done — `mako_mail.h` · `mail_smtp_test.mko` · `examples/mail_program.mko` |
| Go-style templates (if/range/with/define, HTML escape) | Done — `mako_template.h` · `template_test.mko` · `examples/template_demo.mko` |
| fmt / print packages (Sprintf/Print/Errorf, multi-arg) | Done — `mako_fmt.h` · `std/fmt` · `std/print` · `fmt_print_test.mko` |
| Hex/dec/bin/oct format + parse (bases 2–36, %#x/%08x) | Done — `format_int_*` / `parse_int_*` · `fmt_print_test.mko` |
| Language residuals wave 40 (deep Send/race, NLL, patterns, stability, GC, reflect, JPEG baseline, Unicode) | Done — `lang_residuals_test.mko` · `nll_multi_label_test.mko` · `api_stable_test.mko` |
| Language residuals wave 41 (Ok(Some) non-generic, exotic `?`, race stack, tracing GC, UCD/PCRE depth) | Done — `lang_residuals_test.mko` · `gc_app/gc_trace_test.mko` |
| UUID v4/v5/v7 + ULID (Copy POD, kick/Send, parse polish) | Done — `runtime/mako_uuid.h` · `uuid_test.mko` · `std/uuid` |
| Speed gate vs Rust (fib/slice/map ≤2×) | PASS — `./scripts/bench-gate.sh` |
| Speed audit: release no longer forces bounds-always; empty str singleton; map 75% load | Done — see PERFORMANCE.md |
| Map set_take (no string-key clone) + HTTP zero-copy views into raw | Done — `map_take_http_test.mko` |
| Header/Content-Type interning + `respond_json` static CT | Done — runtime `mako_http_intern_*` |
| HTTP/2 DATA auto-split (≤16384) + proxy/map free safety | Done — `http2_data_frame` split; `http2_prod_test` |
| `chan_str_send_take` / `try_send_take` (no string clone) | Done — `chan_string_test.mko` |
| Proxy splice polish (256 KiB + sendfile file→socket) | Done — `mako_proxy.h` `tcp_fd_copy` |
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
| Pack-qualified types (`eng.Table` annot / lit / pattern) + multi-return of structs | Done — `pack_types_test` · `tuple_struct_test` |
| Struct eq/hash with slice/map fields (engine tables as packs) | Done — `struct_slice_fields_test` |
| Struct eq/hash with Option/Result/enum fields | Done — `struct_slice_fields_test` · `lang_residuals_test` |
| Pack-qualified enums (`eng.Red` / `eng.Color.Green(n)` construct + match) | Done — `pack_types_test` |
| Maps of structs (`map[int]T` / `map[string]T`, pack types) | Done — `map_struct_test` |
| `make(chan[Struct])` + `maps_*` on II/SS/struct maps | Done — `chan_make_struct_test` · `map_struct_test` |
| `map[int]float` / `map[string]float` + structural maps_equal | Done — `map_float_test` · `map_struct_test` |
| Struct/enum `==` `!=` (structural) | Done — `struct_eq_test` |
| Float map keys (`map[float]int|string|float|Struct`) | Done — `map_float_test` · `map_struct_test` |
| Struct map keys (`map[Point]int|string|float|bool|Struct`) | Done — `map_struct_key_test` |
| `map[Struct]Struct` (named key + named value) | Done — `map_struct_key_test` |
| `map[K]bool` + `[]bool` + `map[bool]V` | Done — `map_bool_test` |
| Enum maps + `[]Enum` (`map[K]Enum`, `map[Enum]V`, …) | Done — `map_enum_test` |
| Nested slices `[][]T` | Done — `nested_slice_test` |
| `map[K][]T` (scalar + named keys × slice values) | Done — `map_slice_test` |
| Nested maps `map[K]map[K2]V` (depth 2) | Done — `map_nested_test` |
| Nested maps depth 3 `map[K]map[K2]map[K3]V` | Done — `map_depth3_test` |
| `map[K][][]T` (nested-slice values) | Done — `map_nested_slice_test` |
| Nested maps with slice values `map[K]map[…][]T` | Done — `map_map_slice_test` |
| `Option[map[K]V]` / `Result[map[K]V]` (all map kinds, match unbox) | Done — `option_map_test` |
| `[]map[K]V` and `map[K][]map[…]` | Done — `slice_map_test` |
| `map[K]Option[T]` / `map[K]Result[T,E]` (bag values) | Done — `map_option_result_test` |
| `[]Option[T]` / `[]Result[T,E]` (bag element slices) | Done — `option_result_slice_test` |
| `map[K][]Option[T]` / `map[K][]Result[T,E]` | Done — `map_option_slice_test` |
| `map[K]Option[[]T]` / `map[K]Result[[]T,E]` | Done — `map_option_of_slice_test` |
| `map[K](T,U)` tuple values (scalar 2–3-tuples) | Done — `map_tuple_test` |
| Map tuples with Struct/Enum + homogeneous 4-tuples | Done — `map_tuple_struct_test` |
| `map[K]Option[map]` / `map[K]Result[map]` | Done — `map_option_of_map_test` |
| `map[K]chan[T]` channel values | Done — `map_chan_test` |
| `map[K][]chan[T]` slices of channels | Done — `map_slice_chan_test` |
| `Option[chan]` / `map[K]Option[chan]` / `Result[chan]` | Done — `map_option_chan_test` |
| Nested channel bags `[]Option[chan]` / `Option[[]chan]` | Done — `map_option_chan_nested_test` |
| `[][]chan[T]` / `(chan, scalar)` map values | Done — `map_chan_nested_slice_tuple_test` |
| 3-tuples with channel field as map values | Done — `map_tuple_chan3_test` |
| Nested `Option[Option[…]]` / `Result[Option[chan]]` maps | Done — `map_nested_option_chan_test` |
| Mixed bag nests `Option[Result]` / triple Option / `Result[Result]` maps | Done — `map_option_result_nested_test` |
| Nested bag slices `[]Option[Option]` / `Option[[]Option]` maps | Done — `map_nested_bag_slice_test` |
| Bag fields in map tuples `(Option[T], U)` / `(Result[T,E], U)` | Done — `map_tuple_bag_test` |
| Demand-driven map monomorph emission (O(used), not N² grid) | Done — AST collection + gated helpers (`1dd3ddf`) |
| Nested bag slices / bag-field map tuples | Done — `map_nested_bag_slice_test` · `map_tuple_bag_test` |
| `len` nil-safe on SI/II/SS maps (and monomorphized maps) | Done — runtime + nested tests |
| Low-ceremony collections ergonomics | Done — [ERGONOMICS.md](ERGONOMICS.md) |

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
| Send-like kick rules (Copy / string / chan / deep-POD struct / Option·Result·tuple of sendables) | Done — `kick_send_test`, POD kick waves, bad `kick_array_arg` / `kick_non_pod` |
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

**Closed (wave 7–39):** join_timeout **flatten** · POD kick + string fields ·
`reflect_value_of` N + nested POD · `Result`/`Option` deep nests · nested
None/Err edges · **`?` int/string/float/bool/struct/slice/map + nested** ·
SOF0 header fields · **`jpeg_is_baseline_gray`** · JFIF shell probes ·
**`jpeg_roundtrip_ok`** · APP layout checks · NLL for/if/match · kick
Result/Option reject · script/category `\p{…}` · expanded TSan · prior work.

**Wave 39 tests:** `examples/testing/wave39_queue_test.mko` · bad
`try_slice_in_void`.

**Pain residuals (language) — Wave 40 close:** see [PAIN_POINTS.md](PAIN_POINTS.md).

1. **Fuller data-race model** — deep Send + Sync; per-kick race stack; mut Option/Result/tuple/enum/array/map captures until join; field/index writes checked; TSan opt-in (`--race`)  
2. **Result/Option edge shapes** — non-generic `Ok(Some(v))`; exotic `?` cross (Option→Result Err("None"), Result→Option None); generic nests (wave18+)  
3. **Stronger NLL multi-label** — const-fold + multi-label break products  

**Stdlib / product (wave 40–41):**

6. Unicode **Lu/Ll/Lo/ASCII/Any/Assigned/Alnum/Word/…** + `\P`/`\X`/`\h`/`\R`/`\N` — full UCD still not claimed  
7. **Viewer Huffman JPEG** — `jpeg_encode_gray_baseline` / `jpeg_is_baseline_huff`  
8. **Reflect non-POD** — Option/Result/array/map fields (chan still rejected)  
9. Symbol-level parity inside Done packages  

**Also:** `#[stable]` / `#[deprecated]`; optional **tracing GC** (`gc_root`/`gc_link`/`gc_collect`, `[package] gc = true`).

---

## External

homebrew-core publish — [Formula/mako.rb](../Formula/mako.rb).
