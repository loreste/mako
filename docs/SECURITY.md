# Mako security

**Security is first-class** — same tier as concurrency, under a speed-first product.

**Product tip:** **0.2.3**.

Mako treats safety as a **compiler and runtime contract**, not a style guide.
The goal: make leaks, memory corruption, and common backend footguns hard to
ship — by construction where possible, by hard errors where not — **without**
making the default hot path slower than Rust.

| Pillar | How it shows up |
|--------|-----------------|
| **Speed** | Secure-by-construction (NLL, crews, params) stays low-overhead at steady state; sanitizers are **opt-in** |
| **Concurrency** | Structured `crew` cancel-joins ordinary kicked tasks; blocked C/FFI calls can delay the join and `detach` is explicit |
| **Security** | Memory, secrets, injection, bounds — hard errors preferred over soft advice |

Guided tour: [The Mako Book §11](book/src/ch11-speed-safety.md) · Speed bar: [SPEED.md](SPEED.md).  
Soundness program: [SOUNDNESS.md](SOUNDNESS.md) · Memory model: [MEMORY_MODEL.md](MEMORY_MODEL.md).

## Principles

1. **Prevent, don't advise** — illegal states should not compile or should abort
   with a clear diagnostic.
2. **No GC** — all packages stay on ownership, shares, and arenas for
   predictable latency. There is no collector mode that can weaken
   hold/share/move rules.
3. **Secure defaults in the stdlib** — parameterized DB APIs, header validation,
   constant-time token compare, and explicit secret wiping.
4. **Speed is the name of the game** — security features that cost cycles stay
   opt-in or debug-only; do not silently tax every release binary.

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
runtime smoke check (`mako test --race`) for the runtime and FFI boundary.
**Uuid is Copy** — free re-read under `hold`, kick without move.

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
Mako provides two layers of protection:

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

### Cryptography & TLS platform (build secure systems in Mako)

Mako does **not** ship a full PKI product or “crypto framework.” It exposes
**building blocks** so you implement protocols safely in Mako:

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
| TLS inspect | `tls_conn_version`, `tls_peer_cn`, `tls_conn_alpn` | Logging / policy |
| HTTP helpers | `https_*` / `oidc_*` (+ `tls_get` / `tls_post` demos) | Verified HTTPS clients; OIDC never downgrades to `http_*` |
| JWT verification | `jwt_verify` (HS256), `jwt_verify_rs256`, `jwt_verify_jwks` | Explicit algorithm/key type; RS256 requires RSA >= 2048 bits |

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
CA PEM paths, DTLS/WebRTC as first-class products.

Tests: `security_test.mko`, `security_crypto_test.mko`, `password_hash_test.mko`,
`bcrypt_test.mko`, `scram_test.mko`, `tls_aead_test.mko`, `tls_server_test.mko`,
`crypto_srtp_blocks_test.mko`, `security_residuals_test.mko`,
`security_product_test.mko`.

### Session security (Done)

Mako's session management, authentication, and authorization toolkit uses
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

Mako has no tracing garbage collector in the compiler, generated C, runtime, or
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

Mako does not claim "memory safe like a proof assistant." It claims **active
prevention** of the failures that hurt backends most: ignored errors, overruns,
leaked tasks, use-after-move, header/SQL injection footguns — with ownership
rules that every Mako package keeps.
