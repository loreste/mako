# Makori security

**Status:** actively hardened toward the product goal of **100% memory-safe
safe Mako**, not formally proven. The ownership model
prevents many classes of memory bugs by construction, and the full test
suite is exercised under ASan (with leak detection disabled) and UBSan with
zero errors. ASan validates invalid accesses, use-after-free, double-free,
and buffer overflows — but not general leak freedom. Edge cases are still
being found and fixed. This is not yet equivalent to a formally verified
memory model.

**Product version:** **0.6.2**.

Mako treats safety as a **compiler and runtime contract**, not a style guide.
The goal: make memory corruption and common backend footguns hard to ship —
by construction where possible, by hard errors where not.

Makori is its own language with its own syntax. Safety decisions are Mako-shaped:
stdlib parity with Go or Rust never requires exposing their unsafe memory
surfaces. Safe APIs must uphold Mako ownership, bounds, cleanup, and
concurrency rules; unsafe or unverifiable integration stays explicit and
outside the safe claim.

| Pillar | How it shows up |
|--------|-----------------|
| **Ownership** | `hold`/`share`/`arena` — deterministic free, no GC |
| **Concurrency** | Structured `crew` cancel-joins ordinary kicked tasks; `detach` is explicit |
| **Bounds** | Array/slice bounds checks in all builds (safe release mode) |
| **Verification** | Full suite runs under ASan, UBSan, and TSan in CI |

Soundness program: [SOUNDNESS.md](SOUNDNESS.md) · Memory model:
[MEMORY_MODEL.md](MEMORY_MODEL.md) · Stdlib gate:
[STDLIB_SAFETY.md](STDLIB_SAFETY.md).

## Slice backing ownership (0.6.2)

Safe slice values preserve value semantics without a tracing garbage collector:

- The C backend stores owned heap slices in atomic refcounted backing allocations. Cloning one is an O(1) retain; mutation and append detach first when backing storage is shared.
- Borrowed views, stack literals, and pool-backed buffers are non-refcounted and never enter the refcount release path. Generated cleanup matches the allocator that produced the backing storage.
- The native backend records owned versus borrowed values across calls and returns, transfers returned ownership to the caller, and cleans nested or discarded temporaries at their last use.
- Struct fields and generated collection helpers follow the same retain, replace, and release rules as local slices.

This contract is covered by adversarial aliasing and clone-storm tests, native differential tests, leak checks, ASan/LSan, TSan, UBSan, and long-running RSS soaks. It is evidence of continued hardening, not formal verification. `unsafe` blocks and unverifiable FFI remain outside the safe-language guarantee.

## Principles

1. **Prevent, don't advise** -- illegal states should not compile or should abort
   with a clear diagnostic.
2. **No GC** -- packages stay on ownership, shares, and arenas for predictable
   latency. There is no collector mode that can weaken hold/share/move rules.
3. **Secure defaults in stdlib** -- parameterized DB APIs, header validation,
   constant-time token compare, explicit secret wiping, verified TLS by default.
4. **Speed is the name of the game** -- security features that cost cycles stay
   opt-in or debug-only; do not silently tax every release binary.

## Footgun Prevention Policy

Safe Mako should make the secure path the short path. APIs that commonly lead
to memory corruption, credential leaks, injection, or silent downgrade must
force an explicit choice: a loud name, an unsafe boundary, or a failing return.

- **No hidden insecure fallback:** TLS, HTTPS, JWT, database, and parser helpers
  fail closed instead of silently downgrading verification, bounds, or
  algorithm checks.
- **Dangerous names are explicit:** helpers such as `*_insecure` are for demos,
  tests, and controlled local development only. They are not part of the safe
  default path and must have a verified alternative next to them.
- **CI can enforce this:** `makori lint --security` fails on known insecure helper
  calls unless the exact line carries `// mako: allow-insecure`.
- **Sanitizer-clean runtime boundaries:** HTTP client DNS/connect uses
  `getaddrinfo` rather than direct legacy hostent pointer loads, keeping the
  network path compatible with UBSan alignment checks.
- **Input injection is rejected at the boundary:** HTTP headers reject CR/LF/NUL,
  SQL has parameterized APIs, URL/path helpers normalize before use, and parsers
  bound lengths before touching buffers.
- **Secrets are not ordinary strings once classified:** keys, bearer tokens,
  password material, and session secrets should use `Secret` plus
  `secret_eq_str`/`const_eq`; docs and examples must not teach `==` for token
  checks.
- **Unsafe stays narrow:** raw memory, unchecked indexes, FFI ownership transfer,
  dynamic loading, and platform-specific handles are excluded from safe parity
  unless wrapped by checked handles with deterministic cleanup.

### Concurrency Send seed (kick)

`crew.kick(f(args…))` only accepts **Send** argument types: Copy scalars
(including float and **Uuid**/ULID POD), **deep-POD structs**, `string` (heap-cloned),
channels, `ShareInt` / `AtomicInt` (RC clone), locked handles (`CMap` / `Mutex` /
`RWMutex`), and **Option/Result/tuple of Send** payloads (heap-boxed across spawn).  
Rejected: arrays, maps, non-POD structs, `Arena`, nested `Crew`
(`examples/bad/kick_non_pod.mko`, `kick_array_arg.mko`).  

The compiler also analyzes every function value and lambda that crosses a
`kick` boundary. Unsynchronized mutable captures are rejected, including
captures hidden behind a function alias; unknown closure environments are
rejected rather than guessed safe. `fan` mappers are required to be
capture-free because their workers have no checked environment. Use
`Mutex`/`RWMutex`/`CMap`/`AtomicInt`/`ShareInt`/channels for intentional shared
state. The adversarial fixtures are
`examples/bad/kick_mutable_closure_capture.mko`,
`kick_mutable_lambda_capture.mko`, and `fan_capture.mko`. TSan remains a
runtime smoke check (`makori test --race`) for the runtime and FFI boundary.
**Uuid is Copy** — free re-read under `hold`, kick without move. The native
backend clones string-like task arguments, including string-backed `Uuid`, at
the `kick` boundary so the child task owns its payload without aliasing the
parent scope.

SMTP TLS: `smtp_send_starttls` uses `SSL_connect`. Set **`MAKO_SMTP_TLS_VERIFY=1`**
to enable peer certificate verification (`SSL_VERIFY_PEER`); default is off for
dev soft paths.

## Memory and resources

| Risk | Mako prevention |
|------|-----------------|
| Integer overflow | `checked_add/sub/mul` + `--overflow trap` mode |
| Leaks (testing) | `leak_mark/check/scope_enter/exit` + `leak_report_json` |
| Leaks (scoped work) | Values and `arena` regions are released at scope end |
| Orphan threads | `crew` **cancel_joins** ordinary kicked jobs on exit; cancellation is cooperative and blocked C/FFI may delay the join |
| Buffer overflow | Array/string index bounds-checked in debug and release; explicit `unsafe` / `unsafe_index` opt-out |
| Use-after-move | CFG NLL + `hold` move checker (`use of moved value`) |
| Secrets in memory | `secret_from_str` / `secret_drop` — wipe via `mako_secure_zero` |
| Raw pointer games | Not available in safe Mako |

### Integer overflow protection (Done)

Integer overflow leads to incorrect calculations, truncated values, and security
vulnerabilities (e.g., buffer size calculations wrapping to small allocations).
Makori provides two layers of protection:

**Checked arithmetic functions** return `Result[int, string]` on overflow:

```mko
match checked_add(a, b) {
    Ok(v) => use(v),
    Err(e) => log_error(e),
}
```

**Compile-time overflow mode** rewrites all `+`, `-`, `*` to abort on overflow:

```bash
mako build --overflow trap main.mko
```

**Predicate functions** let you test without performing the operation:

```mko
if would_overflow_mul(count, size) == 1 {
    return error("allocation too large")
}
```

Runtime: `runtime/mako_overflow.h`.

### Leak detection (Done)

Mako includes a built-in leak detector for catching memory leaks in tests and
during development. Scoped leak checking ensures that code under test frees
everything it allocates:

```mko
fn TestNoLeaks() {
    leak_scope_enter()
    // ... code under test ...
    let leaked = leak_scope_exit()
    assert_eq(leaked, 0)
}
```

For detailed investigation, `leak_bytes_since(mark)` reports bytes allocated
since a snapshot, and `leak_report_json()` produces structured output suitable
for CI integration.

Runtime: `runtime/mako_leak.h`.

### Move checker (Done)

`hold` bindings move on rebind, into calls, and on full reads of non-Copy types.
Use-after-move is a hard type error with a clear hint. See
`examples/bad/hold_use_after_move.mko` and GUIDE § ownership.

### Explicit secret wiping (Done)

```mko
let tok = secret_from_str(api_key)
// ... use ...
secret_drop(tok)   // explicit_bzero / memset_s / volatile wipe
```

Runtime: `runtime/mako_security.h` (`MakoSecret`, `mako_secure_zero`).

### Explicit `unsafe` bounds opt-out (Done)

```mko
unsafe {
    let v = unsafe_index(xs, i)   // no debug bounds check — SAFETY: i in 0..len
}
```

Default indexing stays checked in both debug and release builds. `unsafe_index`
is rejected unless it appears inside an explicit `unsafe { }` block. Use that
escape only when you have proven the index is in range.

### HTTP header validation (Done)

Writers reject CR/LF/NUL and illegal name tokens (`http_header_ok`, Content-Type
in `mako_http_reply_conn`). Injection attempts fail closed.

### Parameterized DB only (Done)

- Unified `sql_*`: `sql_exec` / `sql_query_int` (int params), `sql_exec_str4` /
  `sql_query_str` (string params), `sql_query_rows` / `sql_query_rows_str` (multi-row)
  bind placeholders — SQLite `?`/`$N`, Postgres `$N` (with `?` rewritten).
  Arg count must match placeholders.
- Legacy: `sqlite_query_int` / `_params`; `pg_exec` / `pg_exec_params`.
- Do not build SQL by string-concatenating user input.

### Constant-time compare (Done)

```mko
if const_eq(got, want) == 1 { /* ok */ }
// alias: crypto_eq
if secret_eq_str(secret_from_str(token), presented) == 1 { /* ok */ }
```

### Cryptography & TLS platform (build secure systems in Makori)

Makori does **not** ship a full PKI product or “crypto framework.” It exposes
**building blocks** so you implement protocols safely in Makori:

| Layer | What exists | You build |
|-------|-------------|-----------|
| Digests / MAC | `sha256`/`sha512`/`sha1`, `hmac_sha256`/`hmac_sha1` (+ raw) | Integrity tags, tokens |
| AEAD / stream | `aes_gcm_*`, `chacha20_poly1305_*`, `aes_ctr` | Sealed messages, SRTP CM |
| KDF | `pbkdf2_sha256`, `hkdf_sha256` (RFC 5869), Argon2id, bcrypt | Password storage, key schedule |
| Auth protocols | SCRAM core (`crypto.scram_*`), Digest (SIP) | SASL, REGISTER auth |
| Random | `random_bytes` / `random_int` (OS CSPRNG) | Nonces, session IDs |
| Secrets | `secret_from_str` / `secret_len` / `secret_eq_str` / `secret_drop` | Wipe keys after use |
| TLS server | `tls_server_new` / `tls_accept` / `tls_read`/`write` (+ NB handshake) | HTTPS, STARTTLS terminate |
| TLS client | `tls_client_new` / `tls_connect` / SNI / VERIFY_PEER | Outbound TLS, SIPS, mTLS apps |
| TLS pool / mTLS | `tls_pool_open_timeout` / `_mtls` / `_mtls_full` + client cert paths | General pooled outbound TLS (any app); caller-supplied timeouts |
| TLS inspect | `tls_conn_version`, `tls_peer_cn`, `tls_conn_alpn` | Logging / policy |
| HTTP helpers | `https_*` / `oidc_*` (+ `tls_get` / `tls_post` demos) | Verified HTTPS clients; OIDC never downgrades to `http_*` |
| JWT signing / verification | `jwt_sign` (HS256), `jwt_sign_es256`, `jwt_verify`, `jwt_verify_rs256`, `jwt_verify_es256`, `jwt_verify_jwks` | Explicit algorithm/key type; ES256 requires a P-256 key; RS256 requires RSA >= 2048 bits |

**Secure defaults:** TLS min 1.2; modern cipher suites; client verify and host
name verification when using `tls_client_new(ca)` / `https_*`. Prefer Argon2id for new password storage. Use `const_eq` /
`secret_eq_str` for tokens — never `==` on secrets in hot auth paths if you care
about timing (language `==` is not constant-time).

#### SCRAM-SHA-256 core (`pull "crypto"`)

Mako ships the **RFC 5802 / 7677 crypto schedule**, not a full SASL state
machine. Applications (e.g. Postgres wire servers) own nonces, base64, and
message framing.

```mko
pull "crypto"

// salt = base64_decode(wire_s); auth = bare + "," + server_first + "," + final_wo_proof
let salted = crypto.scram_salted_password(password, salt, iterations)
let stored = crypto.scram_stored_key(crypto.scram_client_key(salted))
if crypto.scram_verify_proof(stored, auth, client_proof) == 1 {
    let v = crypto.scram_server_signature(crypto.scram_server_key(salted), auth)
    // AuthenticationSASLFinal: "v=" + base64_encode(v)
}
```

| Rule | Detail |
|------|--------|
| Salt | Raw bytes in API; decode wire base64 first |
| AuthMessage | `client-first-bare,server-first,client-final-without-proof` |
| Proof check | `scram_verify_proof` recovers ClientKey and compares StoredKey with `const_eq` |
| Storage | Persist `StoredKey` + `ServerKey` + salt + iters — not the plaintext password |
| Channel binding | `scram_gs2_header` / `scram_cbind_b64` / `scram_client_final_without_proof` + `tls_unique` |
| SCRAM-PLUS adoption | `scram_tls_unique_cbind(conn)` / `scram_plus_client_final_bare(conn, nonce)` — only useful after a finished handshake when apps call these (or `tls_unique`) |

API table: [BUILTINS.md](BUILTINS.md) § SCRAM-SHA-256. Std recipe:
[STDLIB.md](STDLIB.md#crypto). Vector test: `examples/testing/scram_test.mko`.
Channel-binding helpers: `examples/testing/security_residuals_test.mko`,
`security_product_test.mko`.

### Encryption at rest (Done)

AES-128-GCM with a random 12-byte nonce prepended (`nonce || ct || tag`):

| API | Role |
|-----|------|
| `seal_at_rest(key, plaintext, aad)` | Seal blob (key = 16 bytes) |
| `open_at_rest(key, sealed, aad)` | Open blob (empty on auth fail) |
| `seal_file_at_rest(path, key, pt, aad)` | Seal + write file |
| `open_file_at_rest(path, key, aad)` | Read + open file |

Pack wrappers: `crypto.seal` / `crypto.open`. Test: `security_residuals_test`.

### Configurable limits (Done)

| API | Role |
|-----|------|
| `limits_new(mem_bytes, time_ms, max_conns)` | Budget handle (`0` = unlimited) |
| `limits_try_mem` / `limits_release_mem` | Charge / release memory |
| `limits_check_time` | Within wall-clock budget? |
| `limits_try_conn` / `limits_release_conn` | Connection slots |
| `limits_mem_used` / `limits_open_conns` | Inspect |
| `limits_free` | Free handle |

### Remote session cancellation (Done)

Process-local registry; pass the token over the wire for multi-node cancel:

| API | Role |
|-----|------|
| `session_cancel_token()` | Mint + register active token |
| `session_cancel(token)` | Mark cancelled (registers if unknown) |
| `session_cancelled(token)` | Poll (1/0) |
| `session_cancel_clear(token)` | Drop registry entry |

### Node mTLS + cert lab (Done)

| API | Role |
|-----|------|
| `tls_server_new_mtls(cert, key, client_ca)` | Require + verify client certs |
| `tls_server_sni_add(server, hostname, cert, key)` | Select a preloaded certificate by exact or left-most wildcard SNI |
| `tls_server_sni_update(server, hostname, cert, key)` | Copy-on-write replacement of an existing SNI context |
| `tls_server_sni_remove(server, hostname)` | Copy-on-write removal; active connections retain their selected context |
| `tls_client_new_mtls(ca, client_cert, client_key)` | Present client cert |
| `tls_unique(conn)` | Finished bytes for SCRAM tls-unique binding |
| `scram_tls_unique_cbind(conn)` | SCRAM-PLUS `c=` from `tls_unique` (apps still own the dialogue) |
| `scram_plus_client_final_bare(conn, nonce)` | `c=…,r=…` bare final with tls-unique |
| `tls_server_reload(server, cert, key)` | Hot-reload server cert/key (rotation without restart) |
| `tls_make_self_signed(cert, key, cn, days)` | Dev/lab self-signed PEMs (OpenSSL) |
| `tls_make_csr(csr, key, cn, bits)` | Write CSR + key PEMs for external signing |
| `pem_count_blocks` / `pem_has_block` / `pem_extract_block` / `pem_load_file` | String-level PEM helpers (no OpenSSL for parse) |
| `path_file_size(path)` | `stat` file size (−1 if missing) — pairs with PEM/cert paths |

Pack wrappers: `crypto.tls.server_new_mtls` / `client_new_mtls` / `unique` /
`server_reload` / `make_self_signed` / `make_csr`; `crypto.x509.*` for PEM +
cert lab; `crypto.scram_tls_unique_c` / `scram_plus_final_bare`.

**Crypto core only:** high-level SASL state machines (full AuthenticationSASL\*
framing, multi-round negotiation) stay **application code** — Mako ships digests,
HMAC, PBKDF2, SCRAM schedule + channel-binding helpers, not a SASL framework.

**Still out of scope (product):** full CA/HSM/ACME product, WebPKI store beyond
CA PEM paths, browser WebRTC as a first-class product (the DTLS-SRTP building
block ships — see below).

### DTLS over UDP (Done — building block)

DTLS 1.2 over a UDP socket (`std/dtls` / `dtls_*` builtins,
`runtime/mako_dtls.h`): the WebRTC DTLS-SRTP handshake primitive.

- **Cookie exchange** — `dtls_accept` runs the RFC 6347 stateless
  HelloVerifyRequest exchange (per-connection HMAC-SHA256 cookie secret), so a
  bound UDP socket is not an amplification/CPU oracle before the peer proves
  its address.
- **Fingerprints** — `dtls_local_fingerprint` / `dtls_peer_fingerprint` expose
  SHA-256 cert digests for SDP `a=fingerprint` pinning; apps authenticate the
  peer by comparing against the signaled fingerprint (there is no WebPKI in
  DTLS-SRTP — the fingerprint IS the trust anchor).
- **Key material** — `dtls_export_srtp_keys` returns raw RFC 5764 SRTP master
  key+salt bytes. Treat it as a secret: use `dtls.export_srtp_secret` (or
  `secret_from_str` + `secret_drop`) so the bytes are wiped on drop instead of
  lingering in the heap.
- **v1 limits** — DTLS 1.2 only, PEM-path certs, one peer per UDP socket
  (after `accept` the socket is `connect()`ed to that peer), POSIX only.

Tests: `examples/testing/dtls_test.mko` · interop: `scripts/dtls-smoke.sh`.

Tests: `security_test.mko`, `security_crypto_test.mko`, `password_hash_test.mko`,
`bcrypt_test.mko`, `scram_test.mko`, `tls_aead_test.mko`, `tls_server_test.mko`,
`crypto_srtp_blocks_test.mko`, `security_residuals_test.mko`,
`security_product_test.mko`.

### Session security (Done)

Makori's session management, authentication, and authorization toolkit uses
defense-in-depth to prevent common web security vulnerabilities.

**Constant-time token-bearing checks.** Cookie, CSRF, bearer, basic-header, and
signed-token verification use `const_eq` internally, preventing timing
side-channel attacks for those secret comparisons:

```mko
// All of these are constant-time by construction:
auth_session_cookie(cookie_hdr, "sid", expected)  // session cookie check
auth_check_bearer(auth_hdr, expected_token)       // bearer token check
auth_check_basic(auth_hdr, user, pass)            // basic auth check
auth_token_check(token, secret)                   // HMAC-SHA256 token verify
csrf_check(expected, submitted)                   // CSRF token verify
```

**HttpOnly cookie defaults.** `cookie_make` validates the name/value and always
sets `HttpOnly` (prevents JavaScript access via `document.cookie`),
`SameSite=Lax` (blocks cross-site POST requests from carrying the cookie), and
`Path=/`. Invalid delimiter/control input fails closed.

**Cryptographic session IDs.** `session_id_new` uses `mako_random_bytes` (backed
by the OS CSPRNG) to generate 16 random bytes, formatted as 32 hex characters.
This provides 128 bits of entropy -- brute-force guessing is computationally
infeasible.

**CSRF token generation and verification.** `csrf_token` generates a random token
for embedding in forms or response headers. `csrf_check` verifies it with
constant-time comparison. Combined with SameSite=Lax cookies, this provides
layered CSRF protection.

**Secret wiping.** Signing keys used with `auth_token_sign` and API tokens can be
stored via `secret_from_str` and explicitly zeroed with `secret_drop`. This
prevents key material from persisting in freed memory:

```mko
let key = secret_from_str("my-hmac-key")
let token = auth_token_sign("user:42", "my-hmac-key")
// ... after use ...
secret_drop(key)   // zeroes memory via mako_secure_zero
```

**HMAC-SHA256 signed tokens.** `auth_token_sign` produces tokens in the form
`subject.signature` using HMAC-SHA256. The signature is verified by
`auth_token_check` in constant time. The subject can be extracted with
`auth_token_subject` without verification (always verify first).

Runtime: `runtime/mako_security.h` (`MakoSession`, `MakoCSRF`, `MakoAuth`).

### Channels + cancel policy (Done)

`crew` exit calls `mako_nursery_cancel_join` — cancel flag set, then all tasks
joined. Cancellation is cooperative for already-running work; a blocked C/FFI
call can delay the join. New kicks after `cancel()` do not start threads. Tests:
`examples/testing/cancel_policy_test.mko`.

### GC-free runtime (Done)

Makori has no tracing garbage collector in the compiler, generated C, runtime, or
standard-library API. The ownership rules are therefore not a mode that can be
weakened by package configuration. A legacy `[package] gc = true` setting is
rejected, and removed `gc_*` calls are compile errors.

## Incremental build cache

Fingerprints cover full safety-relevant inputs; NLL never skipped on a partial
fingerprint. See [BUILD.md](BUILD.md).

## Compiler-enforced checks (today)

### Unhandled `Result`

A `Result` used as a bare statement is an **error**.

### Bounds checks (SAFE-001)

Debug and release (`-O3 -flto -DNDEBUG`): abort on OOB. Generated code defines
`MAKO_SAFE_DEFAULT`. Explicit opt-out: `unsafe` / `unsafe_index` only.

### Diagnostics

Lexer/parser/type errors print `file:line:col`, caret, and `help:` hints.

## What this is not

Makori does not claim "memory safe like a proof assistant." It claims **active
prevention** of the failures that hurt backends most: ignored errors, overruns,
leaked tasks, use-after-move, header/SQL injection footguns — with ownership
rules that every Makori package keeps.
