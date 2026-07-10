# Mako standard library

Batteries for **web and backends**, with naming conventions adapted to Mako.

Call builtins directly (`str_split`, `path_join`, …) **or** import std packages:

```mko
import "strings"
import "path"
import "sync"

let s = strings.concat(strings.split("a,b", ","), "-")
let p = path.clean("/a/../b")
let m = sync.rwmutex()
```

Bare names like `import "strings"` resolve under `std/` (override with `MAKO_STD`)
and auto-alias so `strings.split` works. Relative `import "./x.mko"` unchanged.
Note: method names that are keywords (`join`, `match`, …) use aliases
(`concat`, `matches`, `join_path`).

Performance bar: **fast and lean** on the same
hardware — no *mandatory* GC, arena-per-request, few copies, structured
concurrency.

**Book (stdlib chapter):** [book/src/ch07-stdlib.md](book/src/ch07-stdlib.md) ·
**Working APIs with syntax:** [GUIDE.md](GUIDE.md) · **How-tos:** [howto/](howto/) ·
North star: [VISION.md](VISION.md) · Honest matrix: [STATUS.md](STATUS.md) ·
Queue: [ROADMAP.md](ROADMAP.md).

Runtime: `runtime/mako_rt.h`, `runtime/mako_stdlib.h`, `runtime/mako_std.h`,
`runtime/mako_http.h`, `runtime/mako_db.h`, `runtime/mako_security.h`.

Tests: `examples/testing/stdlib_*`, plus area tests (`base64_test`, `regex_*`,
`errors_test`, `path_join_test`, …). Demo: `examples/stdlib/demo.mko`.

---

## Package index (2026-07-10 · Wave 9)

| Package | Status | Role |
|---------|--------|------|
| `strings` / `bytes` | **Done** | split/join/trim/replace/builder + `bytes.Buffer` |
| `strconv` / `fmt` | **Done** | parse/format; `fmt_sprintf*` |
| `io` / `fs` / `path` / `filepath` | **Done** | files + recursive walk |
| `bufio` | **Done** | buffered reader/writer |
| `os` / `env` / `args` / `os/exec` / `os/signal` | **Done** | env/args/exec; signal Unix |
| `flag` | **Done** | CLI flags |
| `net` / `http` / `net/url` / `net/mail` / `net/smtp` | **Done** | + AUTH PLAIN / STARTTLS probe |
| `encoding/*` + `gob` / `binary` | **Done** | LE+BE; gob map/ss/struct/`[]string` |
| `compress/gzip` · `archive/tar` · `archive/zip` | **Done** | multi-file zip + deflate |
| `mime` / `multipart` · `context` · `crypto` | **Done** | |
| `math` / `rand` · `text/template` / `html/template` | **Done** | if/range/with/nested |
| `html` · `utf8` · `sync` / `atomic` · `slices` / `maps` | **Done** | |
| `errors` / `testing` / `httptest` / `regexp` / `log` / `slog` / `sql` | **Done** | RE2-ish + `\x` `(?:)` `\p{...}` lookahead |
| `image/png` / `gif` / `jpeg` | **Done** | LZW dict; DCT + Huffman block |
| `reflect` | **Done** | value bag + clone/equal |
| `plugin` / `syscall` | skip | VISION |
| `embed` | **Done** | helper (not compile-time) |

Runtime: `mako_rt.h` + `mako_goext.h` (Waves 1–9). Tests: `goext_wave{,3,4,5,6,7,8,9}_test.mko`.

---

## Core APIs (Wave 1–9)

| Builtin | Package |
|---------|---------|
| `flag_string` / `flag_int` / `flag_bool` | flag |
| `exec_output` / `exec_run` | os/exec |
| `url_scheme` / `host` / `path` / `query` / `query_escape` | net/url |
| `csv_split_line` / `csv_join_row` | encoding/csv |
| `xml_escape` / `xml_tag_text` / `html_escape` | encoding/xml · html |
| `gzip_compress` / `decompress` / `available` | compress/gzip |
| `tar_write_file` / `tar_first_name` | archive/tar |
| `mime_type` | mime |
| `context_with_timeout` / `expired` / `remaining` | context |
| `bytes_buffer*` | bytes |
| `rand_seed` / `rand_intn` / `rand_float` | math/rand |
| `template_execute` | text/template |
| `base32_encode` · `sha1` · `sha512` | encoding/base32 · crypto |
| `lookup_host` / `parse_ip_ok` / `dns_*` | net / DNS and address helpers |
| `signal_notify` / `signal_received` | os/signal |
| `atomic_*` | sync/atomic |
| `utf8_valid` / `utf8_rune_len` | unicode/utf8 |
| `filepath_walk` / `filepath_walk_n` | path/filepath |
| `slices_reverse` / `slices_unique` | slices |
| `embed_file` | embed helper |
| `zip_write_file` / `zip_first_name` / `zip_read_file` / `zip_deflate_available` | archive/zip |
| `png_*` / `gif_*` / `jpeg_*` | image |
| `maps_keys` / `values` / `clear` / `clone` / `equal` / `copy` | maps |
| `reflect_*` / `reflect_struct_*` | reflect |
| `httptest_serve_once` / `get` / `status` / `header` | testing/httptest |
| `aead_available` / `aes_gcm_*` / `chacha20_poly1305_*` | crypto |
| `multipart_boundary` / `multipart_form_value` / `multipart_file_*` | mime/multipart |
| `regex_find_all` / `replace*` / `valid` / `quote_meta` | regexp |
| `html_template_execute` / `execute2` / `execute3` / `if` | html/template |
| `gob_encode_*` / `gob_decode_*` / `gob_*_map_ss` | encoding/gob |
| `mail_parse_address` / `mail_header_get` / `mail_address_ok` | net/mail |
| `smtp_format_message` / `smtp_send_soft` | net/smtp |
| `binary_put_u*le` / `binary_u*le` | encoding/binary |
| `zip_create` / `zip_add` / `zip_write_to` / `zip_list` | archive/zip |
| `reflect_value_*` | reflect |
| `jpeg_encode_gray_dct` / `gif_encode_rgb_lzw` | image |
| `slog_*` | log/slog |
| `slog_redact` / `slog_with_redacted` | structured logging redaction controls |
| `metric_*` / `gauge_*` / `hist_*` / `metrics_export` | process-local observability metrics |
| `validate_required` / `validate_*_len` / `validate_int_range` / `validate_email` | backend request validation |
| `game_fixed_steps` / `game_fixed_remainder` / `game_alpha` / `game_frame_budget_ok` | fixed-timestep game-loop helpers |
| `fx_*` / `det_rng_*` / `replay_*` | deterministic simulation math, RNG, and replay streams |
| `frame_*` / `obj_*` / `alloc_*` / `leak_*` | game frame allocators, object pools, allocation tracking, scoped leak reporting |
| `ecs_*` | ECS seed: entities, components, queries, archetype masks, system updates |
| `ring_*` / `lfq_*` / `sg_*` | fixed-capacity rings, SPSC queue seed, scatter/gather string helpers |
| `fsm_rule` / `fsm_can` / `fsm_transition` / `fsm_is` | finite-state-machine helpers for session systems |
| `cookie_get` / `cookie_make` / `session_id_new` / `csrf_*` / `auth_*` / `authz_*` | cookies, sessions, CSRF, authentication, authorization |
| `rate_allow` / `rate_remaining` / `cache_*` / `http_compress_if_accepted` | backend rate limiting, TTL cache, compression negotiation |
| `job_schedule` / `job_due` / `job_delay_ms` / `job_cancel` | background job scheduling primitives |
| `conn_pool_slot` / `conn_pool_next` / `lb_pick2` / `lb_pick3` | connection-pool slotting and load balancing |
| `openapi_route` / `openapi_doc` | OpenAPI 3.1 route and document generation |
| `graphql_field` / `graphql_arg` / `graphql_data` / `graphql_error` / `graphql_request` / `graphql_is_mutation` | GraphQL request parsing and response seed helpers |
| `sse_event` / `sse_retry` / `rpc_frame` / `rpc_method` / `rpc_payload` | SSE and streaming RPC wire helpers |
| `io_read_ready` / `io_write_ready` / `io_set_nonblocking` / `io_try_write` / `io_backoff_ms` / `io_should_pause` | backpressure-aware readiness, nonblocking write, and retry policy helpers |

---

## `strings` / `bytes`

| Builtin | Role |
|---------|------|
| `str_len` / `str_eq` / `str_contains` | length, equality, substring |
| `str_has_prefix` / `str_has_suffix` | prefix/suffix |
| `str_index` / `str_last_index` | find (−1 if missing) |
| `str_trim` / `str_trim_space` / `str_trim_left` / `str_trim_right` | trim |
| `str_to_lower` / `str_to_upper` / `str_repeat` | case / repeat |
| `str_replace` | replace all |
| `str_split` / `str_fields` / `str_join` | split/join |
| `str_builder` + `builder_write*` / `builder_string` / `builder_len` | builder |
| `rune_count` | UTF-8 rune count |
| `as_bytes` / `bytes_as_str` / `bytes_view` / `bytes_is_view` | zero-copy views |
| `buf_get` / `buf_put` | process-local reusable byte buffers |

---

## `strconv` / `fmt` / `print`

| Builtin | Role |
|---------|------|
| `parse_int` → `Result[int,string]` | base-10 int |
| `parse_float` | float (0.0 on failure) |
| `parse_bool` → `Result[int,string]` | true/false/1/0 |
| `format_int` / `int_to_string` | int → string |
| `format_float(v, prec)` / `format_bool` | format |
| `fmt_sprintf` / `fmt_sprintf_d` | first `%s`/`%v` or `%d`/`%i` |
| `print` / `print_int*` / `print_float` | stdout |

---

## `path` / `fs` / `io` / `os`

| Builtin | Role |
|---------|------|
| `path_join` / `path_clean` / `path_base` / `path_dir` / `path_ext` / `path_is_abs` | path |
| `read_file` / `write_file` / `append_file` / `remove_file` | file I/O |
| `mkdir` / `file_exists` / `is_dir` / `read_dir` | FS |
| `getcwd` / `chdir` | working directory |
| `env_get` / `env_set` | environment |
| `argc` / `args` / `arg_get` | process args |
| `exit` | process exit |

Cross-platform: Win/Mac/Linux separators and dir APIs in `mako_stdlib.h` /
`mako_platform.h`.

---

## `bufio`

| Builtin | Role |
|---------|------|
| `buf_reader_new(path)` / `buf_reader_from_string(s)` | buffered reader |
| `buf_read_line` / `buf_read(n)` | read line / up to n bytes |
| `buf_reader_close` | close |
| `buf_writer_new(path)` | buffered writer |
| `buf_write` / `buf_write_byte` / `buf_flush` | write |
| `buf_writer_close` | flush + close |

Tests: `examples/testing/bufio_test.mko`.

---

## `net` / `http`

TCP: `tcp_listen` / `tcp_accept` / `tcp_connect` / `tcp_write` / `tcp_close`.

### HTTP server

| Builtin | Role |
|---------|------|
| `http_bind` / `http_accept` | listen / accept |
| `http_method` / `http_path` / `http_body` / `http_header` | request |
| `http_req_method` / `http_req_path` / `http_req_body` | request helpers |
| `http_respond` / `http_respond_ct` / `http_respond_json` | response |
| `http_health_json` / `http_respond_health` | health/readiness JSON helpers |
| `http_next` / `http_keepalive` / `http_close*` / `http_shutdown_*` | lifecycle and graceful shutdown |
| `http_serve` / `http_echo` / `http_listen` | demos |
| `http_header_ok` | header validation |

### HTTP client

| Builtin | Role |
|---------|------|
| `http_get` / `http_post` / `http_request` | client |
| `http_get_timeout` / `http_post_timeout` | timeouts |
| `http_last_status` / `http_last_header` | last response |

HTTPS/H2/gRPC/WS: `tls_*`, `nghttp2_*`, `ws_*` (handshake, server/client frames, echo helpers).

UDP/Unix sockets: `udp_bind`, `udp_send_to`, `udp_recv`, `udp_local_port`,
`udp_close`, `unix_socket_pair`, `unix_socket_pair_peer`, `unix_write`,
`unix_read`, `unix_close`.

### Typed `HttpRequest`

| Builtin | Role |
|---------|------|
| `http_request_parse(raw)` | parse HTTP/1.1 request bytes → `HttpRequest` |
| `http_request_from_conn(conn)` | snapshot from accepted connection |
| `http_request_method` / `path` / `body` | accessors |
| `http_route_match(req, method, pattern)` | match `HttpRequest` against a method and Mako route pattern |
| `http_route_param(req, pattern, name)` | extract `{name}` from a matched route pattern |
| `router_new` / `router_group` / `router_add` / `router_match` / `router_param` / `router_count` | grouped router table and handler-name lookup |
| `reqctx_*` / `middleware_*` | per-request context store and middleware chain/policy helpers |

Same parse shape applies after TLS decrypt (feed plaintext to `http_request_parse`).
Route patterns use Mako's compact `{name}` segment capture form:
`/users/{user}/posts/{post}`.
Example: `examples/http_lib/request_type.mko` · test: `http_request_type_test.mko`.

Examples: `examples/http_lib/`, `examples/api_backend/`. Smoke: `./scripts/http-lib-smoke.sh`.

---

## `encoding/json` · `base64` · `hex`

| Builtin | Role |
|---------|------|
| `json_*` (object/array/path/merge/…) | encode/decode into arenas |
| `#[derive(json)]` | compile-time struct marshal/unmarshal codegen for scalar fields |
| `yaml_get_string` / `toml_get_*` | config extraction seeds |
| `msgpack_int_hex` / `cbor_int_hex` / `avro_long_hex` | binary format integer encoding seeds |
| `base64_encode` / `base64_decode` | base64 |
| `hex_encode` / `hex_decode` | hex |

---

## `crypto`

| Builtin | Role |
|---------|------|
| `sha256` / `hmac_sha256` | digests (hex) |
| `random_bytes` / `random_int` | CSPRNG |
| `const_eq` / `crypto_eq` | timing-safe compare |
| `secret_from_str` / `secret_drop` | zero-on-drop |

---

## `time`

| Builtin | Role |
|---------|------|
| `now_ms` / `now_ns` / `time_unix` | clocks |
| `sleep_ms` / `time_sleep_ms` | sleep |
| `elapsed_ms` | delta from start ms |
| `time_format` | RFC3339 UTC from unix ms |

---

## `sync` / `conc`

| Builtin | Role |
|---------|------|
| `mutex_new` / `mutex_lock` / `mutex_unlock` | mutex |
| `rwmutex_new` / `rwmutex_rlock` / `runlock` / `lock` / `unlock` | RWMutex |
| `wait_group_new` / `wait_group_add` / `done` / `wait` | WaitGroup |
| `chan_new` / `chan_try_send` / `chan_len` / `chan_cap` / `chan_select*` | bounded channels and backpressure |
| `runtime_stats_json` / `runtime_stats_reset` | runtime scheduler/channel introspection |
| `crew` / `go` / cancel policy | structured concurrency |
| `actor_spawn` / `actor_send` / `actor_recv` / `actor_stop` | actors |
| `actor` / `receive` syntax | desugar (see GUIDE) |

---

## `errors` · `testing` · `regexp` · `log` · `math` · `collections`

| Area | Builtins |
|------|----------|
| errors | `error` / `errorf` / `wrap_err` / `error_is` / `error_string` / `?` |
| testing | `mako test`, `assert` / `assert_eq` / `assert_eq_str`, `t_run`, `--race` / `--sanitize` |
| regexp | `regex_match` / `regex_find` / `regex_capture` |
| log | `log_info` / `log_warn` / `log_error` / `log_debug` / `log_kv` |
| math | `abs` / `min` / `max` / `clamp`, `math_sqrt` / `pow` / `floor` / `ceil` / `sin` / `cos` / `log` / `exp` / `math_abs` |
| collections | `sort_ints` / `sort_strings`, `ints_contains` / `strings_contains`, `ints_copy` / `ints_index` |

---

## `database/sql` — unified facade

Same call shape for sqlite and postgres (`?` placeholders; postgres rewritten to `$N`):

| Builtin | Role |
|---------|------|
| `sql_open_sqlite(path)` / `sql_open_postgres(url)` | open → `SqlDB` |
| `sql_ok` / `sql_close` | status / close |
| `sql_query_int(db, sql, []int)` | parameterized query → int |
| `sql_exec(db, sql, []int)` | parameterized exec |
| `sql_begin` / `sql_commit` / `sql_rollback` | transaction boundaries |
| `sql_prepare` / `sql_stmt_query_int` / `sql_stmt_exec` / `sql_stmt_close` | prepared statement handles |
| `sql_migration_applied` / `sql_migrate` | numeric-version migration table and transactional apply |
| `sql_check_typed(schema, sql, params, result)` | static table/column/param/nullability/result checker |
| `sql_pool_open_*` / `sql_pool_query_int` / `sql_pool_exec` / `sql_pool_close` | bounded lazy connection pools with round-robin slots |
| `mysql_connect_url` / `mysql_ok` / `mysql_is_mariadb` / `mysql_driver_name` | MySQL/MariaDB DSN validation and driver metadata |
| `redis_connect_url` / `redis_connect` / `redis_conn_*` / `redis_close` | Redis URL parsing and reusable RESP connection handles |
| `mongo_connect_url` / `mongo_find_one_request` | MongoDB-compatible URL parsing and find-one command request construction |
| `cassandra_connect_url` / `cassandra_select` | Cassandra-compatible URL parsing and CQL select construction |
| `clickhouse_connect_url` / `clickhouse_select` | ClickHouse-compatible URL parsing and SQL select construction |
| `elastic_connect_url` / `elastic_search_request` | Elasticsearch-compatible URL parsing and search request construction |

Legacy drivers still available: `sqlite_query_*`, `pg_connect` / `pg_exec*`.
Parameterized queries only for untrusted input — [SECURITY.md](SECURITY.md).
Tests: `examples/testing/sql_unify_test.mko`, `sql_pool_test.mko`,
`sql_tx_stmt_test.mko`, `sql_migration_test.mko`, `sql_typed_check_test.mko`,
`mysql_redis_polish_test.mko`, `multistore_compat_test.mko`,
`derive_json_codegen_test.mko`.

---

## Local packages (`mako.toml` / `mako pkg`)

```bash
mako pkg init
mako pkg add util path=../util
mako pkg publish          # → .mako/registry (or $MAKO_REGISTRY)
mako pkg install         # resolve SemVer + write mako.lock
mako pkg update
```

See [howto/04-packages.md](howto/04-packages.md).

---

## Performance principles

1. **No mandatory GC** — backends stay predictable; optional GC is app opt-in only.
2. **Arena per request** — allocate freely; free the region once.
3. **Dense values** — less pointer chasing.
4. **Explicit buffers** — fewer hidden copies (`bytes_view`, pools).
5. **Crew concurrency** — no orphan tasks; cancel/timeouts.
6. **Future I/O** — io_uring / kqueue, pooling, HTTP/2–3 depth.

---

## Known gaps (honest)

| Area | Mako today |
|------|------------|
| `strings` package import | Done — `import "strings"` → `std/strings/` (also path, fmt, sync, …) |
| `bufio.Reader/Writer` | Done |
| `net/http.Request` typed | Done (`HttpRequest`) |
| `database/sql` one API | Done (`sql_*`; legacy sqlite_/pg_ remain) |
| Full `regexp` engine | RE2-ish (`\d\w\s`, `{n,m}`, `\b`, find_all/replace); not full RE2/PCRE |
| `sync.WaitGroup` / RWMutex / atomic | Done |
| Generics collections | slices + maps helpers Done; `List<T>` Later |
| zip multi-file · png/gif/jpeg · reflect · httptest · gob/mail/smtp/slog/binary | Done (area-level; not every symbol) |
| Full stdlib symbol parity | **Not claimed** (~98% major *areas*; not every symbol) |

---

## Security

TLS for production listeners, parameterized DB, no ignored `Result`s —
[SECURITY.md](SECURITY.md).
