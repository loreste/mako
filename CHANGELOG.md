# Changelog

## Unreleased

### Build

- Keep debug type checking and code generation within the default Windows stack limit.
- Add reproducible 1k, 10k, and 100k compiler-scaling fixtures for single-file,
  multi-file, generic-heavy, and backend-shaped projects.

### Runtime

- Preserve task return values when joining native Windows worker threads.
- Support file-descriptor I/O, memory maps, and write-ahead logs on Windows.
- Align unbuffered Windows reads and writes without requiring aligned Mako strings.
- Reject writes through read-only memory maps instead of faulting.

## 0.4.0 — 2026-07-20

**mako0.4.0** (`CARGO_PKG_VERSION`). 362 Mako tests + 80 Rust tests, 0 failures.

### Performance

- **Dead code elimination** — removes unreachable functions/structs/enums (default-on)
- **99.8% C output reduction** — hello.mko: 30,184 → 64 lines generated C
- **Demand-driven type helpers** — tuple/array/channel helpers only when used
- **Fast int-to-string** — direct digit extraction, 24x faster than snprintf path
- **Realloc string concat** — `mako_str_concat_own` reuses buffer on chained ops
- **Compile-time string folding** — concat, contains, prefix, suffix, len, repeat, replace
- **Compile-time boolean folding** — `true && false` → `false`
- **Literal len folding** — `len("hello")` → `5`
- **Demand-driven includes** — opt-in `MAKO_LEAN_INCLUDES=1`

### Lint (`mako lint`)

- **Unused imports** — warn when pull/import contributes no reachable functions
- **Unreachable code** — warn on statements after return/break/continue
- **Unused variables** — warn on unused `let` bindings (skip `_` prefix)
- **Shadowed variables** — `mako lint --shadow` or `MAKO_LINT_SHADOW=1`

### Concurrency

- **Seeded channel/select stress:** thread-local select state, replayable seeds under TSan
- Initialize shared select wakeup once without a data race

### Build

- **`--strip`** flag for smaller deploy binaries
- **`MAKO_NO_DCE=1`** escape hatch to disable dead code elimination

### Ownership free

- **Discarded bag free:** explicitly ignored `Option` and `Result` values free
  resolved string, slice, map, struct, nested bag, and error payloads. Borrowed
  bags from fields and indexes remain owned by their container.
- Evidence (ASan): `discarded_bag_drop_test`, `discarded_bag_borrow_test`.

## 0.3.0 — 2026-07-19

**mako0.3.0** (`CARGO_PKG_VERSION`). 360 Mako tests + 79 Rust tests, 0 failures.
**All 11 CI jobs pass** (ubuntu, macOS, Windows, ASan, UBSan, GCC, TSan,
cross-compile, bench gates, claims gate).

### Cross-platform (all CI green)

- Cross-compile to Windows, Linux musl (static), WASI — all pass
- GCC compilation — full suite passes (`_XOPEN_SOURCE 700` for `stack_t`)
- Windows CI expanded: full suite (soft) + 17 core tests (strict)
- ASan with `detect_leaks=0` (focused on use-after-free / out-of-bounds)
- UBSan full suite passes
- IPv6 test timeouts fixed (non-blocking accept, socket timeouts)
- `clock_gettime` shim for Windows cross-compile

### Ownership free (SAFE-006 depth) — no double-free / path-local free

- **Match Own free:** Result/Option pattern payloads (string, slice, map, Err
  string, struct Own fields) free at arm exit unless moved into the match result.
- **Bind-scope free:** Own free records attach to the binding’s scope, not a
  nested if/match arm (fixes early free of outer muts like `out = b` in `if`).
- **Move vs clone on store / arm value:** live registered Own **moves**; aliases
  and field/index borrows **clone** (no double-free of one buffer by two freers).
- **Alias mut freer flag:** `let mut out = path` emits `out__own`; free is
  `if (out__own) …` so path-insensitive free never frees a still-aliased
  caller/param buffer when a reassign arm is not taken.
- **Path-local early-return free:** free only names whose bind scope is still
  active; rebind `own_bind_scope` when sequential arms reuse `let blob`; clear
  arm freer flags on arm exit.
- Evidence (ASan): `match_own_free_test`, `double_free_guard_test`,
  `early_return_path_free_test`, `own_branch_regress_test`; leba `main.mko` build.

### Package integrity (PR #5 hardening)

- V2 digest format with reserved `PACKAGE.sha256` name
- Registry verification integrated into build-time dep resolution
- `collect_program_inputs` propagates verification errors
- Legacy unversioned digests remain readable

### Other

- String slice return allowed for params (str_slice always clones)
- Channel element type inference from field receivers
- Merged PRs #5, #6, #7

## 0.2.5 — 2026-07-19

**mako0.2.5** (`CARGO_PKG_VERSION`). 357 Mako tests + 75 Rust tests, 0 failures.

### Memory safety audit

- 10 codegen ownership bugs fixed (return-from-field clone, consumed-argument
  transfer, arena scope suppression, stack-view boxing in Result/Option,
  Ok/Err/Some transfer_own_on_return, struct-borrow from indexing, string array
  reassign shallow-free, save/restore own_drop_live across if-branches, source
  temp moved on reassign, consumed-arg restricted to known functions)
- Full suite passes under AddressSanitizer with zero errors
- Proven regression test: `own_branch_regress_test` fails on pre-fix compiler,
  passes on fixed compiler (verified both directions)

### Package integrity (PR #4 + hardening)

- Immutable publication: reject same name+version republish
- PACKAGE.sha256 content digest computed in staging before atomic rename
- Digest verified on resolution — tampered packages block dependency resolution
- Fail closed on missing digest (deleted = rejected)
- Staged manifest revalidation before publication
- `mako pkg seal` for legacy package migration (TOFU)
- Lockfile content_hash as independent trust anchor
- Scoped package names, symlink rejection, input validation

### LSP v0.5.0

- Hover with real type info (fn signatures, struct definitions, inferred types)
- Inlay hints (inferred types on let bindings without annotations)
- Signature help with ParameterInformation objects

### Infrastructure

- Per-test timeout (60s default, MAKO_TEST_TIMEOUT_SECS override)
- ASan + UBSan + GCC CI jobs added
- Windows CI corrected (valid test paths, core subset)
- Installer: `tar --no-same-owner`, graceful read-only shell RC handling
- Release tarballs normalized (`--owner=0 --group=0`)

### Documentation

- Removed all language comparisons and overstated claims
- Explicitly marked as experimental/alpha
- No unverified performance claims
- Test categories documented (core vs soft-fallback vs live integration)
- Repo description and website meta updated for honesty

## 0.2.4 — 2026-07-18

**mako0.2.4** (`CARGO_PKG_VERSION`).

Soundness and efficiency release after 0.2.3: memory-safe drops by construction,
stack POD array lits, closed SAFE/RT residuals, build-time lockfile verification.

### Soundness (SAFE / RT)

- **Always-on release bounds** (SAFE-001); ownership categories in the language
  spec (SAFE-002).
- **Owning slice/map free** at scope exit, reassign, break/continue, return
  transfer + materialize-before-free (SAFE-003/004/006); nested `[][]T`
  free-on-reassign via `*_release_replaced` (no shared-inner UAF).
- **`string_view`** surface + `str_as_view` / `str_to_owned` (SAFE-005); owning
  strings free on reassign/scope exit.
- **`?` early-return** frees live owns (SAFE-006).
- **Capture matrix** for kick/fan (SAFE-008); arena/slice escape rejects
  (SAFE-007); CMap gate (SAFE-009); concurrency memory model (SAFE-010).
- **RT-001…006:** crew cancel; opt-in `sched_set_workers` pool +
  `mako_spawn_blocking`; channel take-send ownership; census; select stress seed.
- **Struct Own free:** deep free of string/slice fields on drop.

### Speed

- Stack POD array literals (`cap==0` views); empty slices allocate nothing.
- Escape heapify (`mako_*_array_to_owned`); cold free (`MAKO_UNLIKELY`).

### Packages

- Build-time verification of locked dependencies (PR #3): content hashes,
  verified snapshots, fail-closed merge.

### Other

- Complete TLS certificate chains (`SSL_CTX_use_certificate_chain_file`).
- Field/index mut roots; empty `[]` lit; chained slice index assign.

Tests: `string_view_test`, `struct_own_drop_test`, `sched_pool_test`,
`capture_matrix_test`, `channel_ownership_test`, `nested_arr_drop_test`,
`try_drop_test`, `string_drop_test`, `stack_array_lit_test`, `own_drop_*`,
`pkg::` unit suite.

## 0.2.3 — 2026-07-18

**mako0.2.3** (`CARGO_PKG_VERSION`).

Security patch after 0.2.2: fail-closed JWT JSON/JWKS parsing, safer JWT
sign/verify resource handling, dual-stack HTTP listen, and docs for the
verified HTTPS contract.

### JWT / JWKS

- **Strict JSON value skip** — numbers, `true`/`false`/`null`, objects, and
  arrays with depth limiting; reject trailing junk after a complete object.
- **JWKS fail-closed** — malformed JSON and unknown primitives no longer skip
  as harmless metadata (`jwt_verify_jwks` returns 0).
- **`jwt_sign` / `jwt_verify`** — payload size cap, HMAC length checks, free
  signature buffers on all exit paths.

### HTTPS / listen

- Dual-stack HTTP listen via the shared TCP backlog helper (wildcard binds).
- TLS live tests use free ports and assert SNI listeners bind.

### Docs

- Document verified HTTPS vs cleartext `http_*`, OIDC, and JWT SNI contracts.

## 0.2.2 — 2026-07-18

**mako0.2.2** (`CARGO_PKG_VERSION`).

Security and packaging patch after 0.2.1: concurrent-safe multi-cert SNI,
verified TLS hostnames, HTTPS/OIDC client helpers, JWT RS256/JWKS verify, and
SHA-256 package lock integrity.

### TLS / SNI

- **Copy-on-write SNI sets** — `tls_server_sni_add` rebuilds and swaps the full
  certificate set under the server mutex so accept callbacks never see a
  half-updated table.
- **`tls_server_sni_update` / `tls_server_sni_remove`** — strict replace and
  remove of named SNI certificates (exact or `*.example.com` wildcards).
- **Hostname verification** — when peer verify is enabled, client connect sets
  `X509_VERIFY_PARAM_set1_host` from the SNI hostname.

### HTTPS / OIDC / JWT

- **`https_request` / `https_get` / `https_post`** — TLS HTTP client with optional
  CA PEM path and timeout; `https_last_status` / `https_last_header` for the
  last response.
- **`oidc_discovery` / `oidc_token`** — OpenID Connect discovery document and
  token endpoint helpers over HTTPS.
- **`jwt_verify_rs256` / `jwt_verify_jwks`** — strict base64url JWT signature
  verification with PEM public keys or JWKS JSON.

### Packaging

- **Lockfile integrity v2** — deterministic SHA-256 digests over package sources;
  `pkg install` fails closed on mismatch (from 0.2.1 tip merges).

### Prior tip notes (0.2.1 and earlier)

See sections below for generics, stdlib-in-Mako, and match safety.

### v0.1.10 — Deepen generics

- **Package lock integrity v2** — manifests and recursive `.mko` sources are
  hashed with deterministic SHA-256 digests over normalized relative paths.
  `pkg install` now verifies every reused lock entry and fails closed on source
  changes and missing, modified, or unreadable content. Malformed lockfiles,
  unsafe serialized paths, and unreadable transitive manifests are rejected;
  legacy v1 locks require an explicit `pkg update`.
- **Multi-certificate TLS SNI** — `tls_server_sni_add` preloads exact and
  left-most wildcard certificate contexts. Exact names win, then the longest
  wildcard suffix; malformed/duplicate hostnames and invalid key pairs fail
  closed. Selection is synchronized for concurrent accept loops.
- **Cross-target artifact verification** — Windows GNU PE32+, x86-64/ARM64
  Linux musl static ELF, WASI WebAssembly, and Intel macOS Mach-O outputs are
  checked for target architecture in CI. Windows-GNU cross builds no longer
  collide with MinGW's `clock_gettime`/`nanosleep` declarations, and WASI uses
  explicit portability fallbacks for unsupported POSIX process APIs.
- **Stdlib surface gate** — all 71 checked-in stdlib package files are
  type-checked in the claims gate; fixed stale `slog` and boolean-print wrappers.

- **Multi-statement lambda bodies** — lambdas can now contain `let` bindings,
  assignments, `if`/`else`, `while` loops, and nested control flow. Previously
  only single expressions were supported. Enables real callbacks in stdlib.
- **`mut self` on methods** — `on Type { fn m(mut self) { self.field = v } }`
  passes the receiver by pointer so mutations persist in the caller. Enables
  real iterators and any method that modifies state.
- **Generic enum variant disambiguation** — multiple instantiations of the
  same generic enum (e.g. `MyBox[int]` and `MyBox[string]`) no longer collide
  on shared variant names. Qualified lookup uses return type context to resolve
  the correct instantiation in both type checker and codegen.

### v0.2.1 — Safety & correctness

- **Match exhaustiveness** — compiler error when match arms don't cover all
  enum variants, Option (Some/None), Result (Ok/Err), or Bool (true/false).
  Extended to generic enums via `Type::Named` resolution.
- **Match guards** — `pattern if condition => body`. Guard condition combined
  with pattern into single if-condition. `Some(n) if n > 10 => "large"`.
- **Fix: guarded arms no longer exhaust** — `Some(n) if n > 10` alone does not
  cover all `Some`; missing unguarded arm was a compile-time hole that aborted
  at runtime (`non-exhaustive match`).
- **Fix: `Some(struct)` / nested struct match** — track struct payload names for
  builtin `Some`/`Ok` so `match o { Some(Point { x, y }) => … }` codegen unboxes
  the heap payload correctly (was treating it as `int64_t`).
- **Fix: SQL pool double-release** — `Pool.release` ignores duplicate idx.
- **Fix: HTTP router longest path match** — prefer longer path prefixes over
  first-registered shorter ones.
- **JSON `escape_str`** — also escapes `\b` `\f` and other C0 controls as `\u00XX`.
- **Use-after-move detection** — 48 test cases verify that `hold` values
  produce compiler errors on use after move, partial moves, moves across
  control flow branches (break/continue/if).
- **Compile-time race detection** — mutation of locals while kicked tasks may
  use them is flagged as a compiler error.

### v0.2.0 — Stdlib in Mako

- **io package** (`std/io`) — `StringReader` with `read(mut self)` advancing
  position, `ByteWriter` with `write(mut self)` appending bytes, `drain()`
  reads to completion. Written in Mako, not C wrappers.
- **collections** (`std/collections/stack.mko`) — `IntStack`, `StrStack`,
  `IntQueue` with `push`/`pop`/`enqueue`/`dequeue` using `mut self`. Returns
  `Option` for empty cases. Queue auto-compacts on dequeue.
- **context** (`std/context`) — `Context` struct with `background()`,
  `with_timeout(ms)`, `with_deadline(ms)`, `with_cancel()`, `done()`,
  `err()`, `remaining()`. Deadline propagation and cancellation.
- **encoding/json** (`std/encoding/json`) — `ObjectBuilder` and `ArrayBuilder`
  for constructing JSON incrementally. `set_string`/`set_int`/`set_bool`/
  `set_null`/`set_raw` with proper escaping. Parser wrappers for extraction.
- **net/http** (`std/net/http`) — `Request`/`Response` types, `Router` with
  `add()`/`match_route()` for method+path routing, `text_response`/
  `json_response`/`html_response` constructors.
- **database/sql** (`std/database/sql`) — `Pool` with `acquire()`/`release()`
  slot management, `begin_tx`/`commit_tx`/`rollback_tx` transaction helpers.

### Channels

- **`chan_len` / `chan_cap` on any `chan[T]`** — type-check accepts struct,
  tuple, enum, and string channels (not only `chan[int]`). Runtime helpers
  `mako_chan_ptr_len` / `mako_chan_ptr_cap` and `mako_chan_str_len` /
  `mako_chan_str_cap` back the pointer and string rings.
- **`chan[tuple]` codegen** — `TypeExpr::Tuple` and generic `chan[T]` forms
  register correctly in `chan_ptr_elems` so len/cap and ptr helpers stay
  consistent with `chan[Struct]`.

## 0.1.9 — 2026-07-16

**mako0.1.9** (`CARGO_PKG_VERSION`).

Patch after 0.1.8: **generic types**, **interface bounds**, and seed **iterator /
mutable-closure** infrastructure — the foundation for writing the stdlib in Mako.

### Generics

- **Generic structs** — `struct Pair[T] { a: T, b: T }`, multi-param
  `struct Triple[A, B] { … }`. Monomorphized at compile time to one C struct
  per concrete instantiation (`Pair__int`, `Pair__string`, …).
- **Generic enums** — `enum MyBox[T] { Val(T), Nothing }` with match on
  monomorphized variants.
- **Generic functions returning / accepting generic types** — e.g.
  `fn make_pair[T](a: T, b: T) -> Pair[T]`.
- **Nested generics** — `Box[Pair[int]]` and multi-instantiation in one unit.

### Interface bounds

- **`fn f[T: Iface](…)`** — type parameters may name a structural interface
  bound; call sites check method sets (`on T { fn m… }` satisfies `Iface`).
- Negative: `examples/bad/generic_bound_fail.mko` rejects types that lack
  required methods.

### Iterator protocol (seed)

- Types with a `Type_next` method returning `Option[…]` participate in
  `for … in` codegen.
- **Limitation:** by-value `self` does not mutate the outer iterator; loops that
  only return `Some(self.current)` without advancing will not terminate.
  Prefer explicit mut patterns until mut-self iterators land.

### Mutable closures (seed)

- Codegen detects assignments to captured locals and routes them through a
  heap cell (`malloc` + pointer in env).
- Existing by-value captures and struct captures remain supported.
- Full multi-statement mutable lambdas remain residual polish.

### Docs / packaging

- Roadmap rewritten around 0.1.9 → 1.0 themes; `ROADMAP_IMPL.md` is the
  implementation plan for agents.
- Tests: `generic_*_test`, `iterator_test`, `mutable_closure_test`.

## 0.1.8 — 2026-07-16

**mako0.1.8** (`CARGO_PKG_VERSION`).

Patch after 0.1.7: **speed-first** runtime and codegen wave — hashing, strings,
channels/select, HTTP table scale, and compiler allocation cuts. Memory-safety
and concurrency correctness retained (or tightened) on every change.

### Speed optimizations

- **wyhash replaces FNV-1a** — map hashing now processes 8 bytes at a time
  instead of byte-by-byte. 4-8x faster for string-keyed maps on typical
  workloads. Uses 128-bit multiply mixing (hardware `__uint128_t` where
  available, portable fallback elsewhere).
- **Stack-based f-string builder** — string interpolation (`f"..."`) uses a
  256-byte stack buffer instead of two heap allocations (struct + buffer).
  Short interpolated strings (routes, log lines, error messages) never touch
  the allocator.
- **Compile-time constant folding** — integer binary expressions with literal
  operands (`1 + 2`, `flags & 0xff`, `size > 0`) are folded at compile time.
  No runtime code emitted for arithmetic on constants.
- **Zero-copy string comparisons** — `x == "literal"` and `x != "literal"` use
  `mako_str_view` for the literal side (zero allocation, points directly into
  read-only data). Extends to `str_eq`, `str_has_prefix`, `str_has_suffix`,
  `str_contains`, `match` arm string patterns, and `print("literal")`.
  Previously every string literal in these contexts allocated via
  `malloc + memcpy`.
- **Codegen `want_map` lookup** — demand-driven monomorph checks use a joined
  key set instead of allocating `String` pairs on every call. Cuts thousands
  of heap allocations during compilation of map-heavy programs.
- **Codegen `emit_line`** — hot emission paths write with `format_args!` into
  the output buffer (no intermediate `String` per line).
- **HTTP connection table scaled** — `MAKO_HTTP_CONN_MAX` raised from 32 to
  1024. Active connection count tracked via atomic counter instead of O(n)
  linear scan.
- **HTTP header interning optimized** — length-bucketed `switch` dispatch
  replaces sequential string comparison. Headers of non-matching length are
  skipped without any comparison.
- **`select` condvar wakeup** — channel `select` waits on a shared condition
  variable instead of 2 ms nanosleep polling. Send/close notify waiters for
  near-zero wakeup latency (50 ms max wait slice covers race windows).

### Memory safety & concurrency

- **Channel `cap` lock-free** — `chan_cap()` no longer takes the mutex to read
  an immutable field. Removes unnecessary contention on high-throughput
  channel workloads.
- **Slice append aliasing safety** — append grow path correctly uses
  `malloc + memcpy` (not `realloc`) to preserve sub-slice interior pointer
  safety. `realloc` on a sub-slice's aliased pointer is undefined behavior.

### Bug fixes

- **`http_active_connections` always returned 0** — the atomic active-count
  added with the 1024-slot connection table was never incremented/decremented
  on accept/close. Live transitions now go through `mako_http_conn_set_live`,
  which keeps the counter in sync. Graceful-shutdown drain paths can rely on
  a correct count again.
- **msgpack test expectations** — fixed `TestBinaryFormatSeeds` to match
  spec-compliant compact encoding (positive fixint, uint8, int8) instead of
  expecting always-64-bit format.

### Storage / domain P0–P4 product surface

**P0 — first-class handles + bloom rebuild**
- Domain handles (`Bloom`, `PageMan`, `Predict`, `MultiMap`, …) map to real C
  pointers (params / returns / struct fields), not `int64_t`.
- `bloom_clear` resets bits without free/new.

**P1 — range · multi-value · string keys**
- Range buffer grows (TLS 128 → heap up to 65 536); `range_cap`.
- Iterator: `range_rewind` / `range_next` / `range_key` / `range_val`.
- `MultiMap` multi-value ordered map (`multimap_put` / `get_all` / `range`).
- String keys: `bloom_add_str` / `bloom_maybe_str`, `btree_put_str` /
  `get_str` / `range_str`, `str_hash64`.

**P2 — durable sidecars**
- `btree_save` v2: magic `MBT2` + FNV checksum (legacy v1 load still works).
- `pman_write_page` / `pman_read_page` (full 4 KiB bulk).

**P3 — ergonomics**
- `str_slice_ci_index` / `str_slice_ci_starts`, `builder_write_slice`.
- `file_append2` / `file_append3` (writev multi-record flush).
- Domain registry: `domain_reg_put_*` / `get_*` / `del` (int slots for handles).

**P4 — extras**
- `sst_build8` / `sst_build_n` (N≤8 pairs without C arrays).
- Profile JSON schema remains `mako.profile_samples.v1` (stable).

Tests: `TestDomainHandleFieldsAndFns`, `TestDomainStoragePolishP0toP4`.

## 0.1.7 — 2026-07-15

**mako0.1.7** (`CARGO_PKG_VERSION`).

Patch after 0.1.6: binary codecs (CBOR/MessagePack/Avro), list combinators,
GraphQL/protobuf packages, named timezone offsets.

### Avro · GraphQL package · protobuf package · TZ offsets

- Avro binary: long/bool/null/string/array[long] encode·decode (`std/encoding/avro`).
- GraphQL: is_query, operation_name, request_vars, has_field, data2 (`std/graphql`).
- Protobuf package wrappers over wire helpers (`std/encoding/protobuf`).
- Named fixed offsets (`time_offset_named` UTC/EST/PST/JST/…) + `time_format_offset`.
- Tests: `avro_graphql_tz_test`.

### CBOR + MessagePack + list combinators

- MessagePack encode/decode: int/bool/nil/string/`[]int` (binary + hex helpers).
- CBOR encode/decode: int/bool/null/text/array[int] + `cbor_type`.
- List combinators: take/drop/zip/find/count/any/all, map_add/mul, filter_lt/gt,
  fold_add/mul, take_while_lt (int mono-style).
- Packages: `std/encoding/msgpack`, `std/encoding/cbor`; collections wrappers.
- Tests: `cbor_msgpack_test`.

## 0.1.6 — 2026-07-15

**mako0.1.6** (`CARGO_PKG_VERSION`).

Patch after 0.1.5: YAML/TOML encoding packages, plugin product, rich collections,
full time + syscall, unicode/utf8 depth.

### YAML + TOML encoding

- YAML: get string/int/bool, has, keys, list items, pair/merge encode.
- TOML: flat + `[section]` get, int/bool/float, encode pairs/sections.
- Packages: `std/encoding/yaml`, `std/encoding/toml`.
- Tests: `yaml_toml_test`.

### Plugin product · rich collections · full time · full syscall

- **Plugin product:** live dylib load/call (ping/echo/version), host log callback,
  find-by-name, reload, manifest artifact parse, `plugin_info_json`,
  `std/plugin` package; `plugin_product_test` compiles a real plugin with `cc`.
- **Rich collections:** set ops, min-heap, lock-free ring, sum/min/max/concat/
  fill/range/binary_search; expanded `std/collections`.
- **Full time:** UTC calendar (`time_date` / year…weekday), RFC3339 parse/format,
  local format, duration helpers, trunc/add/sub; `std/time` + `time_full_test`.
- **Full syscall:** portable OS surface (pid/uid/host/uname/pipe/dup/read/write/
  access/chmod/symlink/rlimit/…); `std/syscall` + `syscall_full_test`.

### Full unicode + utf8 package

- UTF-8: encode/decode at offset, last-rune, full_rune, rune_start, valid_rune,
  constants (`rune_error` / `rune_self` / `max_rune` / `utf_max`).
- Unicode UCD seed: `unicode_is_*` categories, case (`to_lower`/`to_upper`/
  `to_title`/`simple_fold`), `unicode_is(prop, r)` (same tables as `\p{…}`).
- Packages: `std/unicode`, expanded `std/unicode/utf8`.
- Tests: `unicode_full_test`.

### List[T] + richer collections

- `List[T]` / `List<T>` aliases `[]T` with correct codegen for element types.
- List helpers: new/push/pop/get/len/clear/insert/remove (int + string).
- Stack peek + queue pop; `slices_reverse_strs` / `slices_unique_strs` /
  `strings_index` / `strings_copy`.
- Package: expanded `std/collections`.
- Tests: `collections_list_test`.

### Plugin as rich package

- Host: name/version/kind/path/abi, alive/count/max_slots, last_error,
  close_all, free_string copy-out, 16 slots.
- Package: `std/plugin` (open/call/close + meta + hot-reload wrappers).
- Tests: `plugin_package_test` (+ residual plugin seeds).

## 0.1.5 — 2026-07-15

**mako0.1.5** (`CARGO_PKG_VERSION`).

Patch after 0.1.4: package-per-directory, unbuffered rendezvous channels,
Go-style implicit interfaces, actor int payloads, const-fn depth (match /
while / for / break·continue / strings / string `const fn`), error chain peel,
and `fallthrough`.

### Const fn string params / returns

- `const fn f(s: string) -> string` and mixed int/string params.
- Int-returning const fns may bind string locals (`len_greet`).
- Typecheck validation uses dummy `""` / `0` for params by type.
- Tests: `TestConstFnString` in `const_fn_test`.

### Const string seed

- `const S = "…"` string constants; `+` / `str_concat` at const time.
- `str_len` / `len` and `==` / `!=` / `str_eq` fold to ints.
- Runtime uses of string consts emit `mako_str_from_cstr("…")`.
- Tests: `TestConstString` in `const_fn_test`.

### Const-fn break / continue

- Bare `break` / `continue` fold in const `while` / `for` / C-style `for`.
- C-for `continue` still runs the post clause (Go/C semantics).
- Labeled break/continue not folded in const (error with hint).
- Tests: `TestConstFnBreakContinue` in `const_fn_test`.

### Const-fn for loops

- Const `for i in n` / `for i in range n` (count 0..n-1).
- Const C-style `for init; cond; post` (let/assign init + post).
- Same 100k iteration cap as const while.
- Tests: `TestConstFnFor` in `const_fn_test`.

### Const-fn depth (match · while)

- Const `match` on ints: literals, `a \| b`, `_`, binding patterns.
- Const `while` with `let`/`assign` (capped at 100k iterations).
- Mirrored in typecheck fold and codegen const env.
- Tests: `TestConstFnMatch` · `TestConstFnWhile` in `const_fn_test`.

### Actor int message payload seed

- `receive Inc(delta)` / `receive Set(v: int)` — one int payload per message.
- Packing: high 16 bits tag, low 48 bits signed payload (`actor_pack` /
  `actor_msg_tag` / `actor_msg_payload`).
- Constructors: `Counter_Inc()` → pack(tag, 0); `Accum_Add(10)` → pack(tag, 10).
- Tests: `TestActorIntPayload` · `TestActorPackHelpers` in `actor_test`.

### Implicit interface method sets (Go-like)

- Types that define `on T { fn m… }` or free `fn T_m(self T, …)` implement
  interface `I` when signatures match — no `on T : I` required.
- Vtable / coercion also resolve `Concrete_method` (alongside `I_method` /
  `I_Concrete_method`).
- Test: `iface_implicit_test.mko`.

### Package-per-directory · unbuffered rendezvous channels

- **Package-per-directory:** all non-test `.mko` files in a package dir merge as
  one unit (same `pack` name enforced). Path deps and `pull` qualify after the
  full merge so cross-file calls rewrite correctly.
- **Entry merge:** compile/check/test of `main.mko` / `lib.mko` / `*_test.mko`
  (and dirs with `mako.toml`) pull sibling units; flat demo dumps stay single-file.
- **Unbuffered channels:** `chan_new(0)` / `chan_open[T](0)` true rendezvous —
  send waits until recv takes; `try_send` only succeeds with a waiting receiver.
  `chan_cap` reports `0`. Int / string / pointer channels.
- Tests: `pkg_per_dir_test` · `chan_rendezvous_test` · example `examples/pkg_per_dir`.

### Seeds & syntax (error chain · fallthrough)

- **Richer error chain seed:** `error_unwrap` / `error_root` / `error_as_tag` /
  `error_has_tag` (Go `errors.Is` / unwrap / tag style on wrap chains).
- **`std/errors`:** `unwrap` / `root` / `as_tag` / `has_tag` aliases.
- **`fallthrough`:** Go dual keyword; last statement of a `switch` `case` merges
  the next arm body (`fallthrough_test`).
- **IDENTITY** errors track → 100%; dual-form checklist ~94%.
- Tests: `error_chain_test.mko` · `fallthrough_test.mko`.

## 0.1.4 — 2026-07-15

**mako0.1.4** (`CARGO_PKG_VERSION`).

Patch release after 0.1.3: language zero-copy string regions, storage polish,
observability/debugger seeds, packaging dry-runs, comptime `if` fold, and
product-path seeds (DAP stdio, profile-serve, live plugin reload).

### Product-path seeds (DAP stdio · profile-serve · live plugin reload · soft FB)

- **DAP:** `mako dap --stdio` Content-Length loop; more commands (scopes/variables/step/breakpoints).
- **Profile service:** `mako profile-serve --port N --max-requests K` continuous HTTP seed.
- **Live dylib reload:** `hot_reload_plugin_watch` / `poll` / `call` / `close` / `swaps`.
- **Soft window FB:** `gfx_window_fill` / `set_pixel` / `get_pixel` / `pixels`.
- **CI:** `.github/workflows/product-seeds.yml` packaging + cross-compile dry-run.

### Comptime depth · hot-reload depth · prediction seed

- **const fn:** comparisons (`== < <= > >=`), `&&`/`||`/`!`, statement `if`/`else`,
  and `if`-expressions fold at compile time (`const_fn_test`).
- **Hot reload:** `hot_reload_note_swap` / `swap_count` / `stamp` / `status_json`.
- **Netcode:** `predict_*` client prediction service seed (input + reconcile).

### DAP dispatch · profile HTTP · cross-target dry-run

- **`dap_handle_request` / `dap_request_seq`** — one-shot DAP request dispatch.
- **CLI:** `mako dap --request '…'` (or stdin) prints a seed response.
- **Profile HTTP seed:** `profile_http_route("/debug/pprof/text|json")` ·
  `profile_pprof_http_body` (app owns the TCP listener).
- **Cross-target:** `scripts/cross-target-seed.sh` lists FreeBSD/RISC-V triples;
  `.github/workflows/cross-target-seed.yml` dry-run.

### Residual roadmap seeds (DAP · pprof · packaging · domain · interop)

- **DAP JSON:** `dap_initialize_response` / `dap_stopped_event` / `dap_threads_response` /
  `dap_request_command` (adapter helpers; lldb remains DWARF path).
- **pprof-text:** `profile_samples_pprof_text` · per-sample `tid` ·
  `profile_sample_thread_count`.
- **Packaging:** `packaging/windows/mako.wxs` · `package-msi-seed.sh` ·
  `package-notarize-seed.sh` · `publish-homebrew-tap-seed.sh` ·
  `publish-winget-seed.sh` · `.github/workflows/package-seed.yml`.
- **Domain:** `gfx_poll` / `gfx_backend_name` · `gpu_metal_ok` / `cuda_ok` /
  `vulkan_ok` stubs · `snap_diff` / `snap_apply_delta` · `netcode_*`.
- **Interop / reload:** `plugin_open` / `plugin_call` / `plugin_close` ·
  `ffi_abi_name` · `hot_reload_unwatch` / `hot_reload_watch_count`.
- Tests: `residual_seeds_test.mko` (+ extended domain/profile tests).

### Sampling CPU profiler seed

- `profile_sample_clear` / `once` / `start` / `stop` / `count` / `len`
- `profile_sample_cpu_us` / `profile_sample_wall_ns` / `profile_samples_json`
- Cooperative stack capture always; POSIX `SIGPROF` + `setitimer(ITIMER_PROF)` when available
- Tests: `examples/testing/profile_sample_test.mko`

### Debugger · OTLP · installer · actor/interface seeds

- **Debugger depth:** source-line soft BPs (`debug_line_bp_*`), frame stack
  (`debug_push/pop_frame` / `debug_frames_json`), async parent ids on tasks,
  optional SIGTRAP (`debug_trap_enable`), combined `debug_snapshot_json`.
- **OTLP:** `trace_export_otlp_pb` (minimal protobuf wire) · `otlp_http_export` /
  `otlp_export_traces_json|pb` · `http_request_ct` (Content-Type).
- **Installer:** doctor validates manifest schema/version/prefix; Windows
  `install.ps1` writes `mako.install.v1`; matrix `DOCTOR_STRICT=1` option.
- **Actors:** `Name_spawn_cap(n)` · **Interfaces:** `on Concrete : Iface { … }`
  desugars to `Iface_Concrete_method`.
- Tests: `fn_drop_debug_test`, `observability_depth_test`, `actor_test`,
  `iface_on_iface_test`.

### Storage polish seeds (bloom · range · page manager)

- **Bloom filter:** `bloom_new` / `add` / `maybe` / `len` / `free` — int64 keys,
  fixed bitset, no false negatives.
- **Ordered range scans:** `btree_range` / `sst_range` (inclusive lo..hi) fill a
  TLS buffer; read with `range_len` / `range_key_at` / `range_val_at` (cap 128).
- **Disk page manager:** `pman_open` / `alloc` / `set` / `get` / `sync` / `pages` /
  `reads` / `writes` / `close` — 4 KiB file-backed pages (superblock + user pages).
- Tests: `TestBloomFilter`, `TestBtreeAndSstRange`, `TestDiskPageManager`.

### Core string region ops (no substring alloc)

- Builtins: `str_slice_eq` / `str_slice_ci_eq` / `str_slice_contains` /
  `str_slice_index` / `str_at_eq` / `str_byte_at` — operate on `s[off:off+len]`
  without allocating a substring.
- General-purpose: parsers, CSV/path/config scanners, text search, any hot path
  that would otherwise allocate `s[i:j]` only to compare or search.
- Tests: `examples/testing/str_slice_zc_test.mko`.

### Zero-copy SIP views (hot path)

- `sip_header_view` / `sip_body_view` / `sip_method_view` + `sip_view_len` /
  `offset` / `eq` / `ci_eq` / `contains` / `copy` (TLS last-view; no malloc on view).
- One-shot: `sip_header_eq` / `sip_header_ci_eq` / `sip_header_contains` /
  `sip_method_eq` — no TLS, no alloc.
- First-line contiguous value only (folds still use owned `sip_header_n`).
- Tests: `TestSipZeroCopyViews`, `TestSipZeroCopyHotLoop` (20k compares).

### Adversarial hardening (SIP NAT + SDP + SQL)

- **Preserve `;maddr`** in `via_fix_source` (only strip received/rport) — RFC 3261 §18.2.2.
- **SDP rewrite:** `sdp_replace_connection_addr` upgrades `IP4`↔`IP6` with the new address.
- **SQL arity:** ignore `$`/`?` inside single-quoted string literals.
- Extra adversarial coverage in `sip_test` (maddr+fix_source, v4→v6 rewrite).

### SDP (RFC 4566) proxy surface

- Media-level parse: `sdp_media_formats`, `sdp_media_connection`/`_addr` (inheritance),
  `sdp_media_attr`, `sdp_media_direction` (default sendrecv).
- Session: `sdp_origin_addr`, `sdp_timing`, `sdp_connection_is_ip6`.
- Proxy rewrite: `sdp_replace_connection_addr`, `sdp_replace_media_port`,
  `sdp_set_media_direction`.
- Build: `sdp_build_audio` (IP6), `sdp_build_av`, `sdp_attr_candidate`.
- Tests: `TestSdpProxyRewrite`.

### SIP NAT full support (RFC 3261 §18.2 + RFC 3581)

- **UAC:** `sip_via_value_rport` — bare `;rport` for symmetric response routing.
- **Ingress:** `sip_via_fix_source` / `sip_msg_fix_top_via` — if `rport` present,
  set `received=src` and `rport=src_port`; else if sent-by ≠ source, set `received` only.
- **Response next-hop:** `sip_via_response_host` (maddr > received > sent-by),
  `sip_via_response_port` (rport value > sent-by port > 5060/5061),
  `sip_via_response_addr`, `sip_msg_response_*`.
- Inspect: `sip_via_has_rport` / `sip_via_rport` / `sip_via_received` / `sip_via_maddr` /
  `sip_via_transport`.
- Tests: `TestSipRfc3581ViaRewrite`, `TestSipRfc3261ReceivedOnly`, `TestSipRfc3581UacAndMsg`,
  `TestSipViaMaddrAndDefaultPort`, IPv6 response addr.

### SIP proxy library positioning

- Documented as the **platform built-in SIP library for proxies** (not a demo seed).
- **`std/sip`** expanded: full proxy surface re-exports (`insert_via`, `strip_via`,
  `via_value_nat`, `digest_response_ha1`, framing, auth challenges, …).
- Prefer pack names in apps to avoid shadowing free `sip_*` builtins.

### SIP RFC compliance polish (3261 / 3581)

- **Via:** force `z9hG4bK` magic cookie; uppercase transport; IPv6 `[addr]:port`
  sent-by; `via_host`/`via_port` parse brackets.
- **RFC 3581:** `via_add_received` strips prior `;rport`/`;received` then rewrites
  (no duplicate bare `;rport`); `via_value_nat` orders `;received` then `;rport`.
- **Record-Route:** `<sip:…;transport=…;lr>`; IPv6 host brackets; lowercase transport.
- **Compact:** `Content-Encoding` ↔ `e`.
- Tests: `TestSipRfc3581ViaRewrite`, `TestSipViaIpv6`.

### SQL bind arity · SIP proxy production surface

- **SQL bind arity:** `sql_exec_str4` uses `mako_sql_placeholder_arity` (max `$N` / `?`
  count). Empty `""` is a real bind value — no more stripping trailing empties
  (fixes Postgres “supplies 1 parameters, requires 2”).
- **`sql_query_str2` / `str3` / `str4`:** multi-arg string queries (same arity rules).
- **SIP ownership:** hot paths use non-owned name literals (`MAKO_SIP_LIT`); framing
  without extra header-buffer copies; compact header aliases; stress test.
- **Proxy helpers:** `sip_insert_via` / `sip_strip_via` / `sip_via_value_nat` /
  `sip_via_add_received` / `sip_via_host` / `sip_via_port` / `sip_record_route` /
  `sip_prepend_header`.
- **Digest HA1:** `sip_digest_response_ha1`, `sip_www_authenticate`,
  `sip_proxy_authenticate`.
- **Framing / To-tag:** `sip_first_message_len`, `sip_ensure_to_tag`,
  `sip_reply_with_to_tag`.
- Docs: GameUDP multi-worker pattern; sip_* shadowing note.
- Tests: `sql_str4_empty_bind_test`, `sql_query_str_multi_test`, `sip_test`,
  `sip_digest_ha1_test`.

### Multi-level LSM · page-backed btree

- **LSM levels:** `levels[3]` L1–L3 SSTs; `lsm_compact_down` promotes/merges L1→L2→L3;
  `lsm_sst_levels` / `lsm_level_len(level)`.
- **Page B-tree:** `pbtree_new` / `put` / `get` / `len` / `pages` / `free` — nodes stored
  in `MakoPage` slots (split/grow).
- Tests: `TestLsmMultiLevel`, `TestPageBTree`.

### Storage polish · hot reload seeds

- **LSM compact:** `lsm_compact(l, path)` merges L0 run (+ prior L1 SST) into a new
  sorted SST, truncates the run; `lsm_compactions` / `lsm_flushes` counters.
- **Crash recovery:** `store_recover_wal(s, w)` replays `P,k,v` / `D,k` WAL records.
- **Hot reload:** `file_mtime_ns`, `hot_reload_watch`, `hot_reload_changed` (mtime slots).
- Tests: `domain_tracks_test` (`TestLsmCompact`, `TestStoreRecoverWal`, `TestHotReloadWatch`).

## 0.1.3 — 2026-07-14

**mako0.1.3** (`CARGO_PKG_VERSION`).

Runtime trust, observability, language ergonomics (closures, f-strings),
storage/domain seeds (no SIPREC/WebRTC), packaging polish, and docs.

### Storage product depth

- **`btree_save` / `btree_load`** — persist ordered KV snapshot to disk.
- **SST:** `sst_build4` / `sst_get` / `sst_len` / `sst_free` (sorted run + binary search).
- **Page cache:** `pcache_new` / `pcache_get` / hits·misses (16-slot LRU).
- **MVCC GC:** `mvcc_gc(min_ts)` / `mvcc_live`.
- **SIMD seed:** `simd_dot_i64_4` / `simd_sum_i64_4`.
- Tests: `storage_depth_test`.

### Domain tracks batch (no SIPREC/WebRTC)

- **B-tree** `btree_new` / `put` / `get` / `len` / `free` (fanout 8).
- **LSM** memtable + run WAL: `lsm_new` / `put` / `get` / `flush` / `attach_run`.
- **MVCC** multi-version get by read ts: `mvcc_*`.
- **Rollback ring** for multiplayer: `rollback_push` / `get` / `restore_slot0`.
- **Graphics soft:** `gfx_window_*`, `gfx_shader_compile`, `gfx_asset_size`,
  `audio_mix`, `physics_step_*`.
- **AI depth host:** RoPE helpers, `kv_cache_*`, `gemm2x2`, `f32_to_f16_bits`.
- **Debug frame:** `debug_set_loc` / `debug_file` / `debug_line` / `debug_frame_json`.
- **Packaging notes:** `package-msi-notes.md`, `package-macos-notarize-notes.md`.
- Runtime: `mako_domain.h`. Tests: `domain_tracks_test`.

### P4 storage depth · game snapshot seeds

- **Hash index:** `hindex_new` / `put` / `get` / `del` / `len` / `free`.
- **Transactional store:** `store_*` with begin/commit/rollback + optional WAL.
- **Snapshots:** `snap_encode*` / `snap_predict` / `snap_reconcile`.
- Tests: `store_index_test`.

### P3 packaging · ShareInt capture · debug locals

- Page/WAL storage seeds · ShareInt closure capture · debug locals/BP registry.
- Packaging: deb/rpm scripts, winget seed, Homebrew formula, validate-matrix.
- Docs: STDLIB / BUILTINS domain tables.

### Language · observability · runtime trust (since 0.1.2)

- Capturing closures (`MakoFn` + env) · string/struct/ShareInt captures · kick Send.
- Auto `fn_drop` on scope exit; kick moves capture env.
- f-string format flags (`+` `#` `-` `0`, hex/oct/bin, float e/f/g).
- OTLP/HTTP JSON export · profile snapshot · stack_trace · crash_report · PGO/LTO.
- P1: portable timeouts · crew first_err · detach · actor state.
- First-class fns · field defaults · tuple chans · struct update (earlier unreleased).

## 0.1.2 — 2026-07-14

**mako0.1.2** (`CARGO_PKG_VERSION`).

### Codegen — demand-driven map monomorphs

- **Used-only emission** for map/bag/slice/tuple/chan monomorph helpers.
  Collects `map[K]V` types from the program AST, then emits only those
  `(key, val)` pairs — not the full N² named-key × bag grid.
- Fixes large-pack monomorph blowup (hundreds of MB–GB of unused `MakoMapK_*` /
  `opt_*` / `arr_*` helpers) so large packs stay roughly O(used maps).
- Nested-map value tags (depth-2/3) and `[]T` bag-array deps still resolve
  correctly when those shapes are actually used.

### Language — Option/Result fields in map tuples

- **`map[K](Option[T], U)`** / **`(U, Option[T])`** / **`(Result[T,E], U)`** and
  same-leaf bag pairs; **`Option[chan[T]]`** × scalar; **`(chan[T], Option[int])`**;
  named-struct bag × int.
- Tuple lits materialize bag ctors for mono tags; map set layout-casts
  compatible `MakoTup_*` (e.g. `None` → `opt_int` retagged to map’s bag leaf).
- Type checker propagates expected tuple element types so `None` / `Ok("…")`
  refine under map assignment.
- Tests: `map_tuple_bag_test`.

### Language — nested bag slices as map values

- **`map[K][]Option[Option[T]]`** / **`[]Option[Result[T,E]]`** /
  **`[]Result[Option[T],E]`** / **`[]Result[Result[T,E],E]`** (scalar, struct,
  channel leaves) — tags `arr_opt_opt_*` / `arr_opt_res_*` / `arr_res_opt_*` /
  `arr_res_res_*`.
- **`map[K]Option[[]Option[T]]`** / **`Result[[]Option[T],E]`** /
  **`Option[[]Result[T,E]]`** / **`Result[[]Result[T,E],E]`** — tags
  `opt_arr_opt_*` / `res_arr_opt_*` / `opt_arr_res_*` / `res_arr_res_*`.
- Array-lit tag inference peels nested `Some`/`Ok` for channel and struct
  payloads.
- Tests: `map_nested_bag_slice_test`.

### Language — mixed bag nests as map values

- **`map[K]Option[Result[T,E]]`** / **`Option[Result[chan[T],E]]`** /
  **`Option[Result[Option[T],E]]`** — tags `opt_res_*` / `opt_res_opt_*`.
- **`map[K]Option[Option[Option[T]]]`** (triple Option, incl. channels) —
  `opt_opt_opt_*`.
- **`map[K]Result[Option[Option[T]],E]`** / **`Result[Result[T,E],E]`**
  (incl. channels) — `res_opt_opt_*` / `res_res_*`.
- Bag monomorphs for scalar / chan / named struct leaves; match unbox propagates
  Result Ok metadata (struct names, nested Option chains) from map temps.
- Tests: `map_option_result_nested_test`.

### Language — nested `Option[Option[…]]` map values + struct-chan 3-tuples

- **`map[K]Option[Option[T]]` / `Option[Option[chan[T]]]`** and
  **`map[K]Result[Option[chan[T]],E]`** — tags `opt_opt_*` / `res_opt_*`.
- **Fix:** `peek_expr_c_ty` treats `Some`/`None`/`Ok`/`Err` as bag C types so
  nested Option metadata is not clobbered (bare `Option[Option[int]]` match).
- **Struct-channel 3-tuples** — `map[K](chan[Point], int, int)` (and mid/last).
- Tests: `map_nested_option_chan_test`.

### Language — 3-tuples with channel fields as map values

- **`map[K](chan[T], U, V)`** and channel in the other two slots —
  `(U, chan[T], V)`, `(U, V, chan[T])` over core channel kinds × scalar pairs.
  Unpack `let c, a, b = t` propagates channel metadata.
- Tests: `map_tuple_chan3_test`.

### Language — `[][]chan[T]` and `(chan[T], scalar)` map values

- **`map[K][][]chan[T]`** — nested channel-slice values (`arr_arr_chan_*`).
- **`map[K](chan[T], U)` / `(U, chan[T])` / `(chan, chan)`** — 2-tuples with
  channel handles (int/bool/float/string/struct × scalars). Tuple lits refine
  float/struct channel mono tags from local metadata; unpack propagates
  send/recv kinds.
- Tests: `map_chan_nested_slice_tuple_test`.

### Language — nested channel bags (`[]Option[chan]` / `Option[[]chan]`)

- **`map[K][]Option[chan[T]]` / `map[K][]Result[chan[T],E]`** — bag-element
  slices of channels (`arr_opt_chan_*` / `arr_res_chan_*`).
- **`map[K]Option[[]chan[T]]` / `map[K]Result[[]chan[T],E]`** — optional /
  fallible channel-slice values (`opt_arr_chan_*` / `res_arr_chan_*`).
- Some/Ok array-lit tagging refined for channel payloads.
- Tests: `map_option_chan_nested_test`.

### Language — `Option[chan[T]]` / `map[K]Option[chan]` / `Result[chan]`

- **Channel bags** — `Option[chan[T]]`, `Result[chan[T],E]`, and as map values
  (`map[string]Option[chan[int]]`, `map[int]Result[chan[string],string]`, named
  keys, float/struct channels). Some/Ok store channel handles via `mako_some_ptr`
  / `mako_ok_ptr`; match unboxes with send/recv metadata.
- Tags `opt_chan_*` / `res_chan_*`. Tests: `map_option_chan_test`.

### Language — `map[K][]chan[T]`

- **Slices of channels as map values** — e.g. `map[string][]chan[int]`,
  `map[int][]chan[string]`, `map[string][]chan[Point]`, named keys.
  Tags `arr_chan_*`; also enables standalone `make([]chan[T], n)` / `append`
  / array lits of channel handles. Float/struct channel metadata propagates
  on slice index.
- Tests: `map_slice_chan_test`.

### Language — nested maps depth 3

- **`map[K]map[K2]map[K3]V`** — three-level nested maps (scalar mid/leaf cores;
  named keys allowed on the outer map). Shallow `maps_*` (pointer identity on
  mid maps). Depth 4+ still rejected.
- Tests: `map_depth3_test`.

### Fix — CI: Windows mutex, nested named-key maps, int-lit array types

- **`session_cancel` mutex** — lazy-init instead of `PTHREAD_MUTEX_INITIALIZER`
  so Windows CRITICAL_SECTION shims compile (was failing every Windows test).
- **`parse_map_k_slice_val`** — if `_map_` is the leftmost separator, defer to
  nested-map parse (`map[Point]map[string][]int` no longer misread as slice).
- **Array lits** — untyped int literals inhabit `[]byte` / `[]int64` / `[]int32` /
  `[]int8` when the element type is expected (annotated lets / casts).

### Language — `map[K]chan[T]` channel values

- **Channel pointers as map values** — e.g. `map[string]chan[int]`,
  `map[int]chan[string]`, `map[bool]chan[float]`, `map[string]chan[Point]`,
  named struct/enum keys. Tags `chan_int` / `chan_string` / `chan_float` /
  `chan_bool` / `chan_Struct`; values are channel pointers (missing key → nil).
  Full get/set/`maps_*`/range/comma-ok; float and struct-channel metadata
  propagates on lookup so `.send`/`.recv` still type-correct.
- **Fix:** clear `chan_float` / `chan_ptr_elems` per function so float/struct
  channel metadata does not leak across functions that reuse local names.
- Tests: `map_chan_test`.

### Security — SCRAM proof compare

- **`crypto.scram_verify_proof`** — uses `const_eq` when comparing the recovered
  StoredKey (was language `==`). Docs: STDLIB / SECURITY / BUILTINS / book ch07 /
  `llms-full.txt` aligned to the real `crypto.scram_*` core API (no fictional
  `scram_client_first` / SASL framing helpers).

### Language — map tuples with Struct/Enum + homogeneous 4-tuples

- **`(Struct|Enum, scalar)` / reverse / `(T,T)`** map values; tags use C mono
  names (`MakoEnum_Color`) so they match `Expr::Tuple` emission.
- **Homogeneous 4-tuples** — `map[K](int,int,int,int)` (and string/float/bool);
  no full 4^4 monomorph grid.
- Tests: `map_tuple_struct_test`.

### Language — `map[K]Option[map[…]]` / `map[K]Result[map[…],E]`

- **Bags of maps as map values** — e.g. `map[string]Option[map[string]int]`,
  `map[int]Result[map[string]int,string]`, struct-valued inner maps, named
  outer keys. Tags `opt_map_*` / `res_map_*`; Some/Ok store map pointers;
  match unbox registers map C types. Fixed named-key parse so `opt_map_…`
  is not misread as a nested-map value.
- Tests: `map_option_of_map_test`.

### Language — `map[K](T, U[, V])` tuple values

- **Tuple map values** — monomorphized `MakoMapS_tup_int_int*`, etc. for
  scalar 2- and 3-tuples over int/string/float/bool keys × same bases. Named
  keys supported. Values stored by value; get/set/`maps_*`/comma-ok.
  Unpack: `let t = m[k]; let a, b = t` (`let a, b = m[k]` is comma-ok, not
  tuple unpack — same as Go).
- Tests: `map_tuple_test`.

### Language — `map[K]Option[[]T]` / `map[K]Result[[]T,E]`

- **Bags of slices as map values** — `map[string]Option[[]int]`,
  `map[int]Result[[]string,string]`, named keys/payloads. Tags `opt_arr_*` /
  `res_arr_*`; Some/Ok heap-box slice headers (existing path). Match on `m[k]`
  registers slice kinds. Fixed named-key parse so `opt_arr_int` is not split on
  inner `_arr_`. Zero-cost monomorphs; bag eq is pointer identity for boxed
  slices (same as Option[map]).
- Tests: `map_option_of_slice_test`.

### Language — `map[K][]Option[T]` / `map[K][]Result[T,E]`

- **Bag-element slices as map values** — monomorphized `MakoMapS_arr_opt_int*`,
  etc. Full get/set/`maps_*`/range; match on `m[k][i]`. Zero-cost: reuses
  `MakoArr_opt_*` / `MakoArr_res_*` already used by `[]Option` / bag-map
  `maps_values`. Equality uses `mako_eq_option_int` / `mako_eq_result_int`.
- Tests: `map_option_slice_test`.

### Security — at-rest, limits, cancel, mTLS, SCRAM cbind

- **Encryption at rest** — `seal_at_rest` / `open_at_rest` (AES-128-GCM,
  `nonce||ct||tag`) and `seal_file_at_rest` / `open_file_at_rest`.
- **Configurable limits** — `limits_new(mem, time_ms, max_conns)` with try/release
  mem & conn slots, `limits_check_time`, inspect helpers.
- **Remote session cancellation** — `session_cancel_token` / `session_cancel` /
  `session_cancelled` / `session_cancel_clear` (process-local registry; share
  token over the wire).
- **Node mTLS** — `tls_server_new_mtls(cert, key, client_ca)`,
  `tls_client_new_mtls(ca, client_cert, client_key)`, `tls_unique(conn)`.
- **SCRAM channel binding** — `scram_gs2_header`, `scram_cbind_b64`,
  `scram_client_final_without_proof` (classic `c=biws` + PLUS-style headers).
- Tests: `security_residuals_test`. Docs: SECURITY.md.

### Language — `[]Option[T]` / `[]Result[T,E]`

- **Bag element slices** — `make([]Option[int], 0, n)`, `append`, index get/set,
  range, and annotated literals `[Some(1), None]`. Same for `Result[T,E]` and
  string/float/bool/Struct payloads. Reuses monomorphized `MakoArr_opt_*` /
  `MakoArr_res_*` (also used by `maps_values` on bag maps). Match on `xs[i]`
  registers Some/Ok kinds; append/index-assign push expected types for bare
  `None` / `Err`.
- Tests: `option_result_slice_test`.

### Docs — howto & book collections surface

- **[howto/10-collections.md](docs/howto/10-collections.md)** — full hands-on guide
  (slices, map key/value grid, sets/groups, nested maps, bag values,
  `Option[map]` / `Result[map]`, `[]map`, `maps_*`, test index).
- **Book** — ch03 map grid + nested/bag examples; ch05 Option/Result×maps;
  ch14 cookbook collections recipes; ch15 appendix map kinds table; ch00/ch10
  cross-links. How-to index, ERGONOMICS, GUIDE, LANGUAGE point at the new guide.

### Language — `map[K]Option[T]` / `map[K]Result[T,E]`

- **Bag values on maps** — `map[string]Option[int]`, `map[int]Result[string,string]`,
  `map[Point]Option[int]`, etc. Values are stored by value (`MakoOptionInt` /
  `MakoResultInt` monomorphs `MakoMapS_opt_int*`, `MakoMapI_res_string*`, …).
  Full surface: get/set/`len`/`has`/`delete`, comma-ok, range, `maps_*`.
  Missing key → zero bag (`None` / `Err("")`). Match on `m[k]` registers
  Some/Ok payload kinds for nested arms.
- **Index-assign expected type** — `m[k] = None` / `Some(x)` / `Ok(x)` / `Err(e)`
  push the map value type as `current_expected` so bare `None` is not
  `Option[int]` when the map holds `Option[string]`.
- Payloads: int/string/float/bool/Struct/Enum. Keys: int|string|float|bool|Struct|Enum.
- Tests: `map_option_result_test`.

### Fixes — `Option[map[K]V]` and annotated `None`/`Some`

- **`let o: Option[map[…]] = None`** — annotation is pushed as `current_expected`
  before checking the init, so bare `None` no longer defaults to `Option[int]`.
- **`Some(map)` / `Ok(map)` codegen** — any `MakoMap*` uses `mako_some_ptr` /
  `mako_ok_ptr`. Match unbox tracks concrete map C types (`MakoMapFI*`,
  `MakoMapBI*`, monomorphized maps, …) not only SI/II/SS. Inferred
  `Some(m)` / `Ok(m)` derive kind + C type from the argument.
- Tests: `option_map_test` (float/bool keys, SI/II/SS, Result map).
- **`[]map[K]V` and `map[K][]map[K2]V`** — slices of map pointers (`MakoArr_map_string_int`,
  …) with make/append/index/range; maps whose values are those slices. Deduped
  nested-arr emission. Tests: `slice_map_test`.

### Fixes — struct eq/hash with composite fields

- **`mako_eq_*` / `mako_hash_*` for structs** — fields that are slices
  (`MakoIntArray`, `MakoArr_*`, …) or map/channel pointers no longer use
  `==` or `(int64_t)` casts (invalid C). Eq/hash use buffer identity
  (`.data` + `.len`) or pointer identity. Unblocks real engine packs
  (e.g. a `Table` with `[]int` + `map[int]int`) after `pull`.
- **`Option` / `Result` / enum struct fields** — same helpers no longer
  emit aggregate `==` or int casts. Runtime `mako_eq_option_int` /
  `mako_hash_option_int` / `mako_eq_result_int` / `mako_hash_result_int`
  (and float-result variants) plus `mako_eq_MakoEnum_*` for enum fields.
  Unblocks `lang_residuals_test` (`WrapOpt` with `Option[int]`) and
  `map[WrapOpt]` / `map[WrapRes]` keys.
- Tests: `struct_slice_fields_test`, `lang_residuals_test`.

### Language — pack-qualified types & multi-return of structs

- **Pack-qualified types** — annotations and return types accept `eng.Table`
  (parsed as the import-mangled name `eng__Table`). Same surface as
  `eng.table_new()` calls.
- **Pack-qualified struct lits & patterns** — `eng.Point { x: 1, y: 2 }`,
  positional `eng.Point { 1, 2 }`, and `match p { eng.Point { x, y } => … }`.
- **Pack-qualified enums** — construct/match `eng.Red`, `eng.Green(n)`,
  `eng.Color.Red`, `eng.Color.Green(n)` (pack alias must not be a value binding).
- **Maps of structs** — `map[int]Point` / `map[string]Point` (and pack types
  e.g. `map[int]eng.Table`): get/set, `len`/`has`/`delete`, comma-ok, `range`.
  Codegen monomorphizes like `[]Struct` (`MakoMapI_*` / `MakoMapS_*`).
- **Struct map keys** — `map[Point]int` / `map[Point]string` / `map[Point]float`
  (and pack keys e.g. `map[eng.Table]int`): monomorphized `MakoMapK_T_i|s|f*`,
  field-wise `mako_eq_T` / `mako_hash_T`.
- **`map[Struct]Struct`** — monomorphized `MakoMapK_Key_vVal*` (second pass after
  all `[]T` helpers); pack types work as key and/or value.
- **`map[K]bool` + `[]bool`** — set-style maps (`MakoMapIB*` / `SB*` / `FB*` /
  `MakoMapK_T_b*`) and bool slices (`MakoBoolArray`): make, append, index, slice,
  range, `maps_*`.
- **`map[bool]V`** — bool keys for int/string/float/bool/Struct values
  (`MakoMapBI*` / `BS*` / `BF*` / `BB*` / `MakoMapB_T*`).
- **Enum maps + `[]Enum`** — `map[K]Enum`, `map[Enum]V`, `map[Enum]Enum`,
  `map[Struct]Enum`, `map[Enum]Struct`, and `[]Enum` with make/append/index.
  Enum keys use `mako_hash_MakoEnum_*` / field-wise eq; unit variants fully
  zero payload slots.
- **Nested slices `[][]T`** — monomorphized outer arrays of slice headers
  (`MakoArr_arr_int` / `arr_string` / … / `arr_Struct`): literals, make,
  append, index, range, sub-slice.
- **`map[K][]T`** — maps with slice values (`MakoMapI_arr_int*`, …) for scalar
  keys × int/string/float/bool/byte/Struct/Enum slices; full get/set/`maps_*`.
- **`map[Struct|Enum][]T`** — named keys with slice values (`MakoMapK_Point_arr_int*`,
  …): same surface as scalar-key slice maps (get/set/len/has/delete, comma-ok,
  range, `maps_*`). Values may be `[]int|[]string|[]float|[]bool|[]byte|[]Struct|[]Enum`.
- **Nested maps `map[K]map[K2]V`** — depth-2 only (inner value must not be a map).
  Outer keys: int|string|float|bool|Struct|Enum; inner maps any previously supported
  leaf map. Values are map pointers (missing → nil); `maps_clone` / `maps_equal` are
  shallow (pointer identity). Tests: `map_nested_test`.
- **`map[K][][]T`** — maps with nested-slice values (`MakoMapS_arr_arr_int*`, …)
  for scalar and named keys × `[][]int|[][]string|[][]float|[][]bool|[][]Struct|[][]Enum`.
  Full get/set/range/`maps_*`. Tests: `map_nested_slice_test`.
- **`map[K]map[K2][]T` / `[][]T`** — nested maps whose values are slice maps
  (e.g. `map[string]map[string][]int`, `map[Point]map[string][]int`,
  `map[string]map[int][][]int`). Leaf specs include slice-value monomorphs;
  parse disambiguates `…_map_…_arr_…` from plain slice values.
  Tests: `map_map_slice_test`.
- **`len` on nil SI/II/SS maps** — `mako_map_{si,ii,ss}_len` treat NULL as 0 (matches
  other map kinds and nested-map zero values).
- **`make(chan[T], n)`** — same element set as `chan_open[T](n)`: int family,
  string, float, bool, **named structs** (incl. pack types).
- **`maps_*` overloads** — `maps_keys` / `values` / `clear` / `clone` / `equal` /
  `copy` work for SI/II/SS, **float-value maps**, struct-value maps, and
  **struct-key** maps.
- **`map[int]float` / `map[string]float`** — full get/set/len/has/delete,
  comma-ok, range, `maps_*` (`MakoMapIF*` / `MakoMapSF*`).
- **Float map keys** — `map[float]int`, `map[float]string`, `map[float]float`
  (`MakoMapFI*` / `FS*` / `FF*`). `+0`/`-0` unify; all NaNs share one key.
- **`map[float]Struct`** — monomorphized `MakoMapF_T*` (incl. pack types).
- **Structural struct equality** for `maps_equal` on struct maps (string
  fields compare by content, not pointer identity).
- **`==` / `!=` on structs and enums** — uses generated `mako_eq_Type` /
  `mako_eq_MakoEnum_*` (field-wise; string content; enum tag + payload).
- **Multi-return of structs** — `let a, b = f()` and tuple match no longer
  force non-primitive tuple elements to `int64_t`. Element C types are taken
  from the registered `MakoTup_*` field list, so local structs and pack-
  prefixed structs (`eng__Table`) unpack correctly.
- Tests: `pack_types_test`, `tuple_struct_test`, `map_struct_test`,
  `map_struct_key_test`, `map_float_test` (float values + float keys),
  `chan_make_struct_test`, `struct_eq_test`; lib `examples/pack_types_lib.mko`.
- Docs: LANGUAGE_SPEC, GUIDE §4b/§4c slices+maps, BUILTINS `maps_*` +
  `chan_open`, ERGONOMICS (maps/slices short path), LANGUAGE, STATUS,
  GO_SYNTAX_CHECKLIST, book ch03/ch15, llms.txt / llms-full.txt.

## 0.1.1 — 2026-07-13 (HTTP/2 production + free safety + CI)

Patch release for production edge stability and CI green. `mako version` reports
**mako0.1.1** (`CARGO_PKG_VERSION`).

### Fixes — HTTP/2 frame size (mako-lang.com)

- **TLS HTTP/2 large responses** — `mako_tls_h2_reply_200` / `404` (and client DATA)
  split bodies into ≤16384-byte DATA frames (`SETTINGS_MAX_FRAME_SIZE` default).
  A single ~19 KiB homepage frame caused browsers to report
  `net::ERR_HTTP2_FRAME_SIZE_ERROR` on https://mako-lang.com/.
- **`http2_data_frame` auto-split** — same rule for raw builders and gRPC helpers;
  `END_STREAM` only on the last frame. `http2_response*` shares that path.
- Tests: `TestHttp2DataFrameSplit`, `TestHttp2ResponseLargeBodySplit`.

### Fixes — empty-string free safety

- Empty-string **singleton** must not be raw-`free`d. Broader use of
  `mako_str_free` in HTTP/2, SIP, WS, reflect, slog, net, cache, proxy forward
  headers/body, `maps_clear_si`, and zip close (avoids `free(): invalid pointer`
  / SIGABRT on macOS).
- Keep raw `free(arr.data)` for int/float/byte arrays (not the string singleton).

### Fixes — CI / portability

- **Windows / cross** — `mako_tcp_shutdown` uses Winsock `SD_*`; `mako_log.h`
  uses platform CRITICAL_SECTION shims + lazy mutex init (no raw `pthread.h`).
- **`game_udp_bind`** — bind `0.0.0.0` (IPv4 any); `"*"` dual-stack IPv6 broke
  IPv4-only game UDP tests.
- **Tests** — ignore `SIGPIPE` so proxy races do not abort under TSan.
- **`http_request` type** — register builtin so client API typechecks.
- **`std/fmt`** — lowercase `int` / `bool` / `float` / `hex` / `dec` aliases.

### Ops / docs

- **mako-lang.com edge** — deploy script and docs for edge TLS `:443` → site
  `:8090` (HTTP/1.1 ALPN until multi-stream H2 is solid on the edge).
- README / LANGUAGE_SPEC version lines match **0.1.1**.

## 0.1.0 — 2026-07-13 (intern + chan take + proxy splice)

### Speed / memory

- **HTTP interning** — common header names and Content-Type values are static
  views; `respond_json` uses interned `application/json; charset=utf-8` (no malloc
  for the type string). Request fill still zero-copy views into `conn.raw`.
- **`chan_str_send_take` / `chan_str_try_send_take`** — move ownership of a string
  into a `chan[string]` without cloning; default `ch.send(s)` still clones (safe).
  Try-send always consumes the temporary (frees on full/closed).
- **Proxy `tcp_fd_copy`** — Linux splice uses 256 KiB chunks + `F_SETPIPE_SZ`;
  Apple/FreeBSD sendfile for regular file→socket before userspace pump (macOS
  declares `sendfile` when `_POSIX_C_SOURCE` would hide it).
- **`http_parse` free safety** — replace raw `free` of default empty fields with
  `mako_str_free` so the empty-string singleton is not freed.
- Tests: `map_take_http_test.mko`, `chan_string_test.mko`, `proxy_edge_test.mko`

## 0.1.0 — 2026-07-13 (map take + HTTP zero-copy)

### Speed / memory

- **`map_si_set_take` / `map_ss_set_take`** — move string keys (and ss values) into
  maps without cloning; default `m[k]=v` still clones (safe).
- **map_ss rehash** — moves owned keys/vals (no clone/free thrash).
- **HTTP zero-copy** — method/path/body/Host/UA/Content-Type are views into the
  connection `raw[]` buffer after a single request copy; header locate via
  `find_header_view` (no intermediate header buffer).
- Tests: `examples/testing/map_take_http_test.mko`

## 0.1.0 — 2026-07-13 (speed audit: release hot path)

### Performance

- **Release bounds default fixed** — `mako build --release` no longer forces
  `MAKO_BOUNDS_ALWAYS` (was silently taxing every index). Opt in with
  `--bounds always` or `[profile.release] bounds_checks = "on"`.
- **Empty string singleton** — `""` / zero-len clone avoids `malloc`; `mako_str_free`
  skips the singleton (safe for map key/value free).
- **Map load ~75%** — fewer rehashes; `MAKO_LIKELY` on map set/get and slice append.
- Bench gate still **PASS** (≤2× baseline on fib/slice/map microbenchmarks).

## 0.1.0 — 2026-07-13 (UUID/ULID + speed/safety)

### UUID / ULID (POD Copy IDs — no GC on the value)

- **v4 / v5 / v7** — `uuid_v4`, `uuid_v5(ns, name)` (SHA-1), `uuid_v7` (unix-ms ordered)
- **Namespaces** — `uuid_ns_dns` / `url` / `oid` / `x500`
- **Format/parse** — string, upper, URN, braces, 32-hex, raw `uuid_bytes` / `uuid_from_bytes` (hard-fail length)
- **Inspect** — `uuid_version`, `uuid_variant`, `uuid_cmp`, `uuid_check`
- **ULID** — `ulid_new` / `ulid_string` / `ulid_parse` / `ulid_timestamp_ms` (same 16-byte POD)
- **Copy + Send** — `Uuid` is Copy (NLL re-read, crew kick heap-boxed pack)
- Pack: `std/uuid` · tests: `examples/testing/uuid_test.mko` (9 tests)
- **Speed gate** — `./scripts/bench-gate.sh` PASS (fib/slice/map ≤2× baseline threshold)

## 0.1.0 — 2026-07-13 (language residuals wave 41)

### Ok(Some) · exotic `?` · race stack · tracing GC · UCD/PCRE depth

- **Non-generic Result[Option[T]]** — param/annotated-let nest metadata; `Ok(Some(v))` codegen
- **Exotic `?`** — Option? in Result[T,string] → Err("None"); Result? in Option → None on Err
- **Race model** — Send/Sync helpers; per-kick capture stack (join pops); field/index writes checked
- **Tracing GC** — `gc_root` / `gc_unroot` / `gc_link` / `gc_mark` / `gc_root_count`; mark-from-roots collect
- **PCRE/UCD** — `\P{…}`, `\X`, `\h`/`\H`, `\R`, `\N`; Alnum/Word/Space/… properties
- **Test harness** — `load_test_package` + test codegen honor nearest `mako.toml` (`gc = true`)
- Tests: `lang_residuals_test.mko`, `examples/testing/gc_app/gc_trace_test.mko`

## 0.1.0 — 2026-07-13 (language residuals wave 40)

### Race / Send / NLL / patterns / stability / GC / reflect / JPEG / Unicode

- **Deep Send** — `kick` accepts Option/Result/tuple of sendables, deep-POD structs,
  and enums with sendable fields; Option/Result heap-boxed across spawn
- **Static race seed** — mutate mut Option/Result/tuple captures before `join` → hard error
- **NLL** — richer const-bool folds (`true == true`); multi-label break tests
- **Patterns** — nested variant patterns (typecheck); struct field patterns `Point { x, y }`
- **API stability** — `#[stable]`, `#[deprecated("msg")]` (call sites hard-error)
- **Optional GC** — `gc_alloc` / `gc_collect` / `gc_live` with `[package] gc = true`
  (forbidden when `systems = true`)
- **Reflect** — Option/Result/array/map fields in `reflect_value_of`
- **JPEG baseline Huffman** — `jpeg_encode_gray_baseline` / `jpeg_is_baseline_huff`
- **Unicode** — `\p{Lu}` / `\p{Ll}` / `\p{Lo}` / `\p{ASCII}` / `\p{Any}` / `\p{Assigned}`
- Tests: `lang_residuals_test.mko`, `nll_multi_label_test.mko`, `api_stable_test.mko`,
  bad: `deprecated_call`, `race_mut_after_kick`

## 0.1.0 — 2026-07-13 (hex / decimal / bases)

### Numeric format & parse (all common bases)

- **Format** — `format_int_dec` / `hex` / `hex_upper` / `hex_prefix` / `hex_pad`,
  `format_int_bin` / `oct` / `format_int_base(n, 2..36)` / `format_pad`
- **Parse** — `parse_int_hex` / `bin` / `oct` / `base` / `auto` (`0x` `0b` `0o`)
- **Sprintf int** — `fmt_sprintf_d("%#08x", n)`, `fmt_sprintf_dd("%d %b", a, b)`
- Packs: `std/strconv`, `std/fmt` · tests in `fmt_print_test.mko`

## 0.1.0 — 2026-07-13 (fmt / print packages)

### Go-style `fmt` and `print`

- **Sprintf** — `fmt_sprintf`…`4`, verbs `%s %v %d %q %x %X %%`, plus
  `fmt_sprintf_d` / `fmt_sprintf_f`
- **Sprint / Sprintln** — space-join; trailing newline variants
- **Print / Println / Printf** — stdout (print without forced newline)
- **Eprint / Eprintln / Eprintf** — stderr
- **Errorf** — format error strings
- Packs: `std/fmt`, `std/print` · tests: `fmt_print_test.mko` · demo: `fmt_demo.mko`

## 0.1.0 — 2026-07-13 (Go-style templates)

### Template language (`text/template` / `html/template`)

- **Engine** — `tmpl_new` / `tmpl_data_*` / `tmpl_execute` / `tmpl_html_execute`
- **Actions** — `{{.key}}`, `{{if}}/{{else}}/{{end}}`, `{{range}}`, `{{with}}`,
  `{{define}}` / `{{template}}`, comments, `len`/`upper`/`lower`/`html`/`printf`
- **HTML mode** — auto-escapes interpolations (`tmpl_html` / `tmpl_html_execute`)
- Packs: `std/text/template`, `std/html/template`
- Tests: `examples/testing/template_test.mko` · demo: `examples/template_demo.mko`
- Legacy `template_execute` / `html_template_*` still work

## 0.1.0 — 2026-07-13 (Email / SMTP package)

### Code email from Mako

- **Message builder** — `mail_msg_*`: From/To/Cc/Bcc, subject, text+HTML
  multipart/alternative, attachments (base64), custom headers, Date/Message-ID
- **SMTP session** — `smtp_new` → connect → EHLO → STARTTLS → AUTH PLAIN →
  MAIL/RCPT/DATA (dot-stuffing) → QUIT; `smtp_last_reply` / `last_code`
- **One-shot** — `smtp_send_msg(host, port, user, pass, msg, use_tls)`
- **Mock SMTP** — `smtp_mock_start` / `serve_once` / `last_message` for e2e
  programming without an external MTA
- Packs: `std/net/mail`, `std/net/smtp` · demos `mail_program.mko`, `send_mail.mko`
- Tests: `examples/testing/mail_smtp_test.mko` (includes full send e2e)

## 0.1.0 — 2026-07-13 (MHA + quant GGUF + BPE)

### Deeper local AI

- **`gpu_mha_f32`** — multi-head attention over `[seq, H·D]` Q/K/V
- **GGUF quant** — `model_load_gguf` dequantizes **Q4_0** and **Q8_0** → f32
- **BPE** — `tok_load_bpe` / `tok_load_merges` / `tok_encode_bpe`
- Tests: `ai_depth_test.mko` (MHA, quant fixture, BPE)

## 0.1.0 — 2026-07-13 (GGUF + transformer kernels + tokenizer)

### Local AI depth for real models

- **GGUF** — `model_load_gguf` loads F32/F16 tensors (quantized types skipped)
- **Transformer kernels** — `gpu_gelu_f32`, `gpu_silu_f32`, `gpu_layernorm_f32`,
  `gpu_transpose_f32`, `gpu_attention_f32` (scaled dot-product, 1 head)
- **Tokenizer seed** — `tok_new` / `tok_load_json` / `tok_load_lines` /
  longest-match `tok_encode` / `tok_decode`
- Still not a full LLaMA runtime — compose layers + load weights in Mako
- Tests: `examples/testing/ai_depth_test.mko` · fixtures `tiny.gguf`,
  `tiny_vocab.json`

## 0.1.0 — 2026-07-13 (local models: existing weights + your own)

### Work with existing models *or* program your own

| Path | Surface |
|------|---------|
| Hosted APIs | `llm_*` (unchanged) |
| **Local weights** | `model_*` + `gpu_*` |

- **`model_new` / `model_set_f32` / tensor introspect** — named f32 tensors on a device
- **`model_load_safetensors`** — Hugging Face safetensors (F32 + F16→f32)
- **`model_save` / `model_load`** — native `.makomodel` for models you author
- **`model_linear_f32`** — dense + bias; `hf=1` for PyTorch `[out, in]` weights
- Compose real nets in Mako (MLP demo); not a full transformer/GGUF runtime yet
- Tests: `model_weights_test.mko` · fixture `tiny_linear.safetensors` ·
  `examples/model_mlp.mko`

## 0.1.0 — 2026-07-13 (GPU AI building blocks)

### GPU seed oriented for AI (not graphics)

North star: **compose inference/training ops in Mako** on multi-vendor GPUs.

- **OpenCL** — NVIDIA / AMD / Intel ICDs + macOS Apple GPU; **host** fallback
- **AI kernels (f32, row-major)** — `gpu_matmul_f32`, `gpu_relu_f32`,
  `gpu_bias_add_f32`, `gpu_saxpy_f32`, `gpu_softmax_rows_f32`, `gpu_sum_f32`
  plus elementwise add/mul/scale/fill
- Dense sketch: `matmul → bias_add → relu` (covered in tests)
- Not a full ML framework (no autograd, no model formats) — primitives only
- Tests: `examples/testing/gpu_seed_test.mko`

## 0.1.0 — 2026-07-13 (GPU OpenCL multi-vendor)

### GPU / accelerator seed (OpenCL + host)

Portable compute for **NVIDIA, AMD, Intel** (OpenCL ICDs) and **macOS** (Apple
OpenCL → GPU), with **host** CPU fallback when no driver:

- **Auto-link** — `-DMAKO_HAS_OPENCL` + `-framework OpenCL` (macOS) or
  `-lOpenCL` (Linux/Windows when headers/ICD found); opt out `MAKO_NO_OPENCL=1`
- **Device** — `gpu_device_open` prefers GPU; `gpu_device_name` / `vendor` /
  `is_gpu` / `backend`; `gpu_opencl_ok`; `gpu_set_prefer_host` for CI
- **Buffers + f32 kernels** — same API on OpenCL or host (`add`/`mul`/`scale`/`fill`)
- Not graphics / user shaders / Metal-native yet (OpenCL covers macOS GPUs today)
- Tests: `examples/testing/gpu_seed_test.mko`

## 0.1.0 — 2026-07-13 (GPU compute seed)

### GPU / accelerator seed (host path)

Initial host-only seed (superseded by OpenCL multi-vendor above).

## 0.1.0 — 2026-07-13 (WebSocket RFC 6455 complete)

### WebSocket production surface

- **Frames** — text/binary/ping/pong/close; 7/16/64-bit lengths (cap 16 MiB);
  FIN + continuation reassembly; RSV rejected
- **Masking** — client→server always masked; server→client unmasked; auto-pong
  uses the correct mask direction
- **Close** — status code + reason; `ws_last_close_code` after close frame
- **Client** — `ws_client_connect` (Happy Eyeballs TCP + upgrade),
  `ws_client_recv`, `ws_client_send_{text,binary,ping,close}`
- **Server** — `ws_accept` / `ws_recv` / `ws_send_*` / `ws_echo` / `ws_echo_once`
- **Status** — `ws_last_opcode`, `ws_last_fin`, `ws_last_status` (0/-1/-2/-3/-4)
- Tests: `examples/testing/ws_api_test.mko` (handshake helpers + loopback e2e)

## 0.1.0 — 2026-07-13 (IPv6 + Happy Eyeballs)

### Networking dual-stack

- **`tcp_listen` / `tcp_listen_addr`** — IPv4, IPv6, dual-stack (`*`/`""` → `::` +
  `IPV6_V6ONLY=0` when supported; fallback IPv4)
- **`tcp_connect` / `tcp_connect_timeout`** — `getaddrinfo(AF_UNSPEC)`, AAAA/A
  interleave, **Happy Eyeballs** racing (default 250ms stagger via
  `tcp_set_he_delay_ms`)
- **Accept / peer / local** — `sockaddr_storage`; IPv6 shown as `[addr]:port`
- **UDP** — IPv6 bind/send/recv for explicit v6 hosts; `udp_bind("*")` remains
  IPv4 for compatibility
- **`tcp_connect_nb`** — first resolved v4/v6 address (nonblocking)
- Tests: `examples/testing/net_ipv6_he_test.mko`

## 0.1.0 — 2026-07-13 (LLM stream / embeddings / retry)

### LLM programming depth

- **`llm_chat_stream`** — true HTTPS SSE read loop; accumulates deltas; returns
  synthetic chat JSON for `llm_content`
- **`llm_chat_retry`** — exponential backoff on 429 / 5xx / connect / rate_limit
- **`llm_is_error` / `llm_error_message` / `llm_should_retry` / `llm_last_status`**
- **Embeddings** — `llm_embed_body`, `llm_embeddings`, `llm_embed`,
  `llm_embedding_dim`, `llm_embedding_json` (OpenAI-compatible `/embeddings`)
- **`llm_body_force_stream`** — ensure `"stream":true` on bodies
- Tests: `examples/testing/llm_test.mko` (12 cases, offline)

## 0.1.0 — 2026-07-13 (strong logging)

### Structured logging (`runtime/mako_log.h`)

Production logging surface:

- **Formats** — logfmt (`ts=… level=… msg=… k=v`) or **JSON lines** (`slog_set_json(1)`)
- **Levels** — default **info**; `slog_set_level` / `slog_get_level` (debug→error)
- **Context** — `slog_set_service`, active `trace=` when set
- **Fields** — `slog_with` / `with2` / `with3` / `with_int`; JSON string escape
- **Output** — `slog_set_output(path)` append file or `""` for stderr; `slog_flush`
- **Redaction** — `slog_redact` / `slog_with_redacted`
- **`log_*` aliases** route through the same backend (filter + format)
- Tests: `examples/testing/strong_log_test.mko` · pack: `std/log/slog`

## 0.1.0 — 2026-07-13 (security / crypto / TLS client)

### Cryptography & TLS

Platform surface so you **build** secure systems in Mako (not a soft PKI product):

- **TLS client (socket-style)** — `tls_client_new` / `tls_client_new_insecure`,
  `tls_connect` / `tls_connect_start` (SNI + VERIFY_PEER), same `TlsConn` I/O as
  server; `tls_conn_version`, `tls_peer_cn`
- **Secrets** — `secret_len`, `secret_eq_str` (constant-time)
- **HKDF-SHA256** — `hkdf_sha256(ikm, salt, info, out_len)` RFC 5869 extract+expand
- Docs: [SECURITY.md](docs/SECURITY.md) capability map; BUILTINS TLS client section
- Tests: `examples/testing/security_crypto_test.mko` (HKDF A.1 vector, secrets, client surface)

## 0.1.0 — 2026-07-13 (SIP platform: build stacks in Mako)

### Position

Mako ships **primitives** so you can implement transaction engines, dialogs,
SIPS, SRTP, proxies, and UAs **in Mako** — not a prebuilt softswitch/WebRTC stack.

### SIP / SDP / RTP (`runtime/mako_sip.h`, `std/sip`)

- SIP parse/build, headers, framing, IDs, URI, Digest, SDP, RTP pack/parse
- Transport wrappers; retransmits/timers = your `crew` + `mono_ns` code
- Tests: `examples/testing/sip_test.mko` · demo: `examples/sip_ua.mko`

### Crypto building blocks for SRTP-in-Mako

- **`aes_ctr(key, iv, data)`** — AES-128/256-CTR (classic SRTP AES-CM keystream)
- **`hmac_sha1` / `hmac_sha1_raw`** — SRTP auth tag source (truncate in Mako)
- Tests: fixed AES-CTR ciphertext vector (OpenSSL-matched) + Digest response hex
  (`10bc49bc…`) + HMAC-SHA1 RFC 2202

## 0.1.0 — 2026-07-13 (database multi-row)

### Multi-row result sets (`sql_query_rows*`)

- **`sql_query_rows(db, sql, []int)`** / **`sql_query_rows_str(db, sql, p1)`** —
  open a result handle (SQLite streams; Postgres materializes).
- **Cursor:** `sql_rows_next` (1/0/-1), `sql_rows_int` / `sql_rows_str` (col),
  `sql_rows_cols`, `sql_rows_ok`, `sql_rows_close`.
- **Bulk first column:** `sql_query_col_int` / `sql_query_col_str` (capped, max 10000).
- Max **32** concurrent result sets per process.
- Tests: `examples/testing/sql_rows_test.mko`

## 0.1.0 — 2026-07-13 (database programming)

### Unified SQL (`sql_*`)

- **`sql_exec_str4` / `sql_query_str` work on SQLite** (were Postgres-only; docs
  claimed both). Placeholders `?` or `$1..$4`; trailing `""` = unused slots.
- **`sql_last_insert_id(db)`** — SQLite `last_insert_rowid`; Postgres `lastval`.
- **`sql_rows_affected(db)`** — rows changed by last mutating statement.
- **Postgres `sql_query_int`** returns the first-column integer (not just 0/-1).
- Parameterized-only for untrusted input — see [docs/SECURITY.md](docs/SECURITY.md).
- Tests: `examples/testing/sql_programming_test.mko`

## 0.1.0 — 2026-07-13 (LLM programming)

### LLM runtime (`runtime/mako_llm.h`, `std/llm`)

First-class **OpenAI-compatible** LLM client focused on market gaps:

- **Messages / bodies** — `llm_message`, `llm_messages_append`, `llm_chat_body`,
  `llm_system_user`, `llm_body_with_tools`
- **Response parse** — `llm_content`, finish reason, usage tokens, tool call
  name/args/count
- **Streaming** — `llm_sse_data`, `llm_sse_delta`, `llm_stream_append`
- **Structured output** — `llm_json_extract` (markdown fences + balanced JSON)
- **Transport** — `llm_https_post` / `llm_chat` / `llm_ask` (HTTPS + Bearer;
  default **xAI** `api.x.ai`, env `XAI_API_KEY`)
- **Ops** — token estimate, retry backoff, key redaction, mono-timeout friendly
- No async coloring; pair with `crew`/`fan` for parallel tool execution
- Tests: `examples/testing/llm_test.mko` · demo: `examples/llm_chat.mko`

## 0.1.0 — 2026-07-13 (low-latency time)

### Clocks

- **`mono_ns` / `mono_us` / `mono_ms`** — monotonic (`CLOCK_MONOTONIC_RAW` when available)
- **`wall_ns` / `wall_us` / `wall_ms`** — wall/REALTIME (logs, calendar)
- **`now_ns`** = mono ns; **`now_ms`** = wall ms (documented domains)
- **`elapsed_ns` / `elapsed_us` / `elapsed_mono_ms`** — mono elapsed (no NTP jump)
- **`deadline_ns` / `deadline_ms` / `deadline_remaining_ns` / `deadline_expired`**
- **`sleep_ns` / `sleep_us`**, **`sleep_until_ns`** (hybrid), **`spin_until_ns`** (busy-wait)
- **`mono_res_ns` / `mono_overhead_ns`** — resolution and sample overhead
- Tests: `examples/testing/time_latency_test.mko`

## 0.1.0 — 2026-07-13 (low-level networking)

### TCP / UDP sockets

- **CLOEXEC** on listen/connect/udp socket create (+ accept)
- **`tcp_peer_addr` / `tcp_local_addr`** — `"ip:port"` via getpeername/getsockname
- **`tcp_write_all` / `tcp_read_n`** — full write and exact-length read
- **`tcp_shutdown` / `tcp_linger` / `sock_error`** — half-close, SO_LINGER, SO_ERROR
- **`udp_bind_addr`** — bind to a specific host
- **`udp_recv` records sender** — `udp_last_sender_host` / `_port` / `_sender`
- **`udp_recv_from`** alias for explicit API
- Tests: `examples/testing/net_lowlevel_test.mko`

## 0.1.0 — 2026-07-13 (filesystem / storage production)

### Filesystem & storage

- **`atomic_write_file`** — temp + fsync + rename (crash-safe config/log updates)
- **`mkdir_all` / `rmdir` / `remove_all`** — parents, empty dir, recursive tree delete
- **`rename` / `copy_file`** — same-FS move and byte copy
- **`is_file` / `path_size` / `file_mtime` / `chmod`** — path metadata
- **`temp_dir` / `temp_file`** — system temp path helpers
- **`symlink` / `readlink` / `realpath`** — links and absolute resolve
- Paths reject **embedded NUL**; recursive remove refuses `/` `.` `..`
- **Direct I/O**: `file_open` always `O_CLOEXEC`; flag bit `32` = exclusive create
- Tests: `examples/testing/fs_storage_test.mko`

## 0.1.0 — 2026-07-13 (HTTP/2 + HTTP/3 hardened path)

### HTTP/2 hardened path (`http2_conn_*`)

- **Dual flow control** — separate send vs recv windows; inbound DATA consumes recv;
  peer `WINDOW_UPDATE` raises send; auto WU restores recv only
- **64 stream slots**, **64 KiB** body buffer, **16 KiB** header assembly
- **PADDED** / **PRIORITY** stripped on HEADERS/DATA; CONTINUATION is pure HPACK
- Body overflow is a **hard error** (no silent truncate); `http2_stream_body_overflow`
- **SETTINGS applied**: max frame size used by `http2_response*`; header table size
  sets HPACK dyn byte budget; initial window delta on open send windows
- **`http2_conn_pump`** auto SETTINGS ACK, PING ACK, WINDOW_UPDATE at 16 KiB
- **`http2_response_ct`**, **`http2_conn_goaway`**, SETTINGS accessors
- `tls_serve_h2_routes` remains a **demo/smoke** helper; production servers use
  `tls_server_new` + `http2_conn_*` (`examples/h2_dynamic_server.mko`)
- Tests: `examples/testing/http2_prod_test.mko`

### HTTP/3 hardened path (quiche)

- **64 KiB** body buffer; overflow drops the request (no silent truncate)
- **32** concurrent QUIC conns, **64** ready requests
- Accessors + **`h3_response`**; POST/PUT/PATCH wait for FIN
- Example: `examples/h3_server.mko` · smoke: `./scripts/h3-server-smoke.sh`

## 0.1.0 — 2026-07-13 (out-of-box one-shot install)

- Full one-shot: **cargo-built binary** + runtime + std — no Rust/cargo on user machine
- Auto-install **clang** on Linux when missing; `env.sh` + shell RC hooks
- `package-release.sh` ships the full `target/release/mako` binary in the tarball
- Release workflow publishes Linux/macOS/Windows binaries on tag **and** workflow_dispatch
- README: prebuilt binary install is the default path

## 0.1.0 — 2026-07-13 (wave 39 queue)

- `?` unwrap for **[]int / []string / []float** and **map** payloads
- Result and Option carriers; Err/None early-return preserved
- Bad: `try_slice_in_void`; TSan wave38
- Tests: `examples/testing/wave39_queue_test.mko`

## 0.1.0 — 2026-07-13 (wave 38 queue)

- `?` unwrap for **struct**, nested **Option**, nested **Result** payloads
- Chained `?`, bool Result `?`; let-binding kind propagation after `?`
- Bad: `try_struct_use_after_err` (Result `?` outside Result fn)
- TSan wave37
- Tests: `examples/testing/wave38_queue_test.mko`

## 0.1.0 — 2026-07-13 (wave 37 queue)

- Fix `?` codegen: Option early-return None; string/float Ok/Some unwrap
- Typecheck: Option `?` only in Option-returning fns; Result `?` only in Result
- Bads: `option_try_in_result`, `result_try_in_option`
- TSan wave36
- Tests: `examples/testing/wave37_queue_test.mko`

## 0.1.0 — 2026-07-13 (wave 36 queue)

- Bool `Result[Result[bool]]` + string `Option[Option[string]]` nests
- `jpeg_app7_length`; `jpeg_has_soi`; **`jpeg_app7_len_matches_payload`**
- while+match+for NLL; share while+for live; Sm/Sk/Pc property seeds
- TSan wave35
- Tests: `examples/testing/wave36_queue_test.mko`

## 0.1.0 — 2026-07-13 (wave 35 queue)

- Float nests: `Result[Result[float]]`, `Option[Option[float]]` Ok/None/Err
- **`jpeg_roundtrip_ok`**; `jpeg_app8_length` / `jpeg_app9_length`
- if+for+match NLL; share match+for live; Common/Mn/Mc property seeds
- TSan wave34
- Tests: `examples/testing/wave35_queue_test.mko`

## 0.1.0 — 2026-07-13 (wave 34 queue)

- `Result[Result[string]]` Ok/inner Err/outer Err edges
- `jpeg_has_app8`/`app9`; **`jpeg_is_mako_dct`** / **`jpeg_is_mako_huff`**
- match+if+for NLL; nested Result reflect reject; Mongolian/Tai_Viet/Inherited
- TSan wave33
- Tests: `examples/testing/wave34_queue_test.mko`

## 0.1.0 — 2026-07-13 (wave 33 queue)

- Bool deep `Result[Option[Result[Option[bool]]]]` Ok/None/Err edges
- **`jpeg_is_mako_raw`**; `jpeg_jfif_app0_length`; `jpeg_app7_payload_len`
- labeled for continue NLL; nested Option reflect reject; Hanunoo/Tagbanwa/Bamum
- TSan wave32
- Tests: `examples/testing/wave33_queue_test.mko`

## 0.1.0 — 2026-07-13 (wave 32 queue)

- String nest `Option[Result[Option[string]]]` Ok/None/Err edges
- `jpeg_has_eoi`; `jpeg_sof0_matches_app7`; **`jpeg_is_mako_complete`**
- labeled for break NLL; nested map reflect reject; Meetei/Phags_Pa/Buhid
- TSan wave31
- Tests: `examples/testing/wave32_queue_test.mko`

## 0.1.0 — 2026-07-13 (wave 31 queue)

- Option-start 5-layer `Option[Result[Option[Result[Option[T]]]]]` Ok/None/Err
- `jpeg_sof0_quant_table`; JFIF thumb W/H; `jpeg_is_mako_jfif` (gray+APP7)
- for+while NLL bad; nested slice reflect reject; Vai/Yi/Glagolitic scripts
- TSan wave30
- Tests: `examples/testing/wave31_queue_test.mko`

## 0.1.0 — 2026-07-13 (wave 30 queue)

- 5-layer `Result[Option[Result[Option[Result[T]]]]]` Ok/Err/None edges
- JFIF density units/X/Y; `jpeg_sof0_component_id`; `jpeg_has_app7` (MAKOJPG)
- while+for NLL bad; reflect chan field reject; Batak/Tai_Tham/Syloti_Nagri
- TSan wave29
- Tests: `examples/testing/wave30_queue_test.mko`

## 0.1.0 — 2026-07-13 (wave 29 queue)

- 4-layer `Option[Result[Option[Result[T]]]]` Ok/None/Err edges
- `jpeg_jfif_major` / `jpeg_jfif_minor`; `jpeg_sof0_sampling` (Hi/Vi)
- match+for NLL bad; reflect map field reject; Ol_Chiki/Limbu/Lepcha scripts
- TSan wave28
- Tests: `examples/testing/wave29_queue_test.mko`

## 0.1.0 — 2026-07-13 (wave 28 queue)

- Deep mixed None/Err (`Ok(Some(Ok(None)))`, mid Err); `Option[[]int]` None
- `jpeg_is_baseline_gray` (JFIF+SOF0 grayscale shell probe)
- for+match NLL bad case; Tai_Le/Kayah_Li/New_Tai_Lue scripts
- TSan wave27 + `select_nll_test`
- Tests: `examples/testing/wave28_queue_test.mko`

## 0.1.0 — 2026-07-13 (wave 27 queue)

- Nested None edges (`Ok(None)`, `Ok(Some(None))`, map Option None)
- `Option[Result]` Some(Err); `jpeg_sof0_components`
- if+match NLL bad case; reflect Result field reject
- Samaritan/Mandaic/Saurashtra scripts; TSan wave26 + crew_drain
- Tests: `examples/testing/wave27_queue_test.mko`

## 0.1.0 — 2026-07-13 (wave 26 queue)

- Result/Option None+Err edges; triple-Result deep Err
- `jpeg_sof0_precision`; match-continue outer NLL; reflect []int field reject
- Lisu/Nko/Tifinagh scripts; TSan wave25 + `job_join_typed_test`
- Tests: `examples/testing/wave26_queue_test.mko`

## 0.1.0 — 2026-07-13 (wave 25 queue)

- Bare `None` takes Option[T] from function return / expected type
- Nested outer/inner Err paths; mono `either` Ok/Err string+float
- `jpeg_sof0_width` / `jpeg_sof0_height` from SOF0 marker
- Match-break NLL bad case; reflect Option field reject; Buginese/Cham/Rejang seeds
- TSan CI: wave24, `crew_fan_test`
- Tests: `examples/testing/wave25_queue_test.mko`

## 0.1.0 — 2026-07-13 (wave 24 queue)

- General deep nest chains for alternating Result/Option layers
- 5-layer `Result[Option[Result[Option[Result[T]]]]]` string/int match
- Bad: `kick_option_non_send`; Balinese/Javanese/Sundanese scripts
- TSan CI: wave23, `kick_share_test`
- Tests: `examples/testing/wave24_queue_test.mko`

## 0.1.0 — 2026-07-13 (wave 23 queue)

- Deeper mixed nests: `Option[Result[Option[T]]]`, `Result[Option[Result[Option[T]]]]`
- Leaf-kind tracking for Option→Result→Option unbox chains (string payloads)
- Bad: `kick_result_non_send`; Telugu/Oriya/Lao scripts; TSan wave22 + kick_sync
- Tests: `examples/testing/wave23_queue_test.mko`

## 0.1.0 — 2026-07-13 (wave 22 queue)

- Nested Ok/Some/None expected-type context (`Ok(Some(Ok(x)))`)
- `Option[Result[T]]` and `Result[Option[Result[T]]]` codegen + match
- Mid-label break NLL bad case; Gujarati/Kannada/Malayalam scripts
- TSan CI: `wave21_queue_test`
- Tests: `examples/testing/wave22_queue_test.mko`

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
