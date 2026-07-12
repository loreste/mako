# Changelog

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

### Networking

- **UDP request/response routing** — `game_udp_sender_addr` (the `host:port` of
  the last sender) and `game_udp_send_to` (send to an arbitrary address). Enables
  forwarding traffic upstream and routing replies back to the original sender.
- multiple UDP frontends by wrapping a `GameUDP` handle in a struct (struct
  arrays hold handles); array literals accept a trailing comma.

### Fixes

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
