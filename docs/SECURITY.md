# Mako security

**Security is first-class** — same tier as concurrency, under a speed-first product.

Mako treats safety as a **compiler and runtime contract**, not a style guide.
The goal: make leaks, memory corruption, and common backend footguns hard to
ship — by construction where possible, by hard errors where not — **without**
making the default hot path slower than Rust.

| Pillar | How it shows up |
|--------|-----------------|
| **Speed** | Secure-by-construction (NLL, crews, params) stays zero-cost at steady state; trap/bounds-always/sanitizers are **opt-in** |
| **Concurrency** | Structured `crew` cancel-joins — no orphan tasks that outlive security boundaries |
| **Security** | Memory, secrets, injection, bounds — hard errors preferred over soft advice |

Guided tour: [The Mako Book §11](book/src/ch11-speed-safety.md) · Speed bar: [SPEED.md](SPEED.md).

## Principles

1. **Prevent, don't advise** — illegal states should not compile or should abort
   with a clear diagnostic.
2. **No mandatory GC** — backends and systems code stay on ownership + arenas
   for predictable latency. An **optional** GC mode may exist for application
   crates only; **`[package] systems = true` forbids GC weakening** of
   hold/share/move rules.
3. **Secure defaults in the stdlib** — parameterized DB APIs, header validation,
   constant-time token compare, zero-on-drop secrets.
4. **Speed is the name of the game** — security features that cost cycles stay
   opt-in or debug-only; do not silently tax every release binary.

### Concurrency Send seed (kick)

`crew.kick(f(args…))` only accepts **sendable** argument types: Copy scalars
(including float), **POD structs** (int/float/bool/**string** fields; heap-boxed,
strings cloned), `string` (heap-cloned), channels, `ShareInt` / `AtomicInt`
(RC clone), and locked handles (`CMap` / `Mutex` / `RWMutex`).  
Rejected: arrays, maps, non-POD structs, `Arena`, nested `Crew`.  
Race detection: `mako test --race` (CI TSan job includes proxy pool/edge). Prefer
channels over shared mutable state.

SMTP TLS: `smtp_send_starttls` uses `SSL_connect`. Set **`MAKO_SMTP_TLS_VERIFY=1`**
to enable peer certificate verification (`SSL_VERIFY_PEER`); default is off for
dev soft paths.

## Memory and resources

| Risk | Mako prevention |
|------|-----------------|
| Integer overflow | `checked_add/sub/mul` + `--overflow trap` mode |
| Leaks (testing) | `leak_mark/check/scope_enter/exit` + `leak_report_json` |
| Leaks (scoped work) | Values and `arena` regions are released at scope end |
| Orphan threads | `crew` **cancel_joins** all kicked jobs on exit |
| Buffer overflow | Array/string index bounds-checked in debug; `unsafe` / `unsafe_index` opt-out |
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

### Zero-on-drop secrets (Done)

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

Default indexing stays checked in debug (`-O0 -g`). Release may elide checks
under `NDEBUG`. Prefer `unsafe { }` only when you have proven the index is in
range.

### HTTP header validation (Done)

Writers reject CR/LF/NUL and illegal name tokens (`http_header_ok`, Content-Type
in `mako_http_reply_conn`). Injection attempts fail closed.

### Parameterized DB only (Done)

- SQLite: `sqlite_query_int` / `_params` bind `?` placeholders; arg count must match.
- Postgres: `pg_exec` / `pg_exec_params` — use `$1..$N`; no concat-SQL happy path.
- Do not build SQL by string-concatenating user input.

### Constant-time compare (Done)

```mko
if const_eq(got, want) == 1 { /* ok */ }
// alias: crypto_eq
```

### Session security (Done)

Mako's session management, authentication, and authorization toolkit uses
defense-in-depth to prevent common web security vulnerabilities.

**Constant-time cookie and token checks.** All session and auth comparisons use
`const_eq` internally, preventing timing side-channel attacks:

```mko
// All of these are constant-time by construction:
auth_session_cookie(cookie_hdr, "sid", expected)  // session cookie check
auth_check_bearer(auth_hdr, expected_token)       // bearer token check
auth_check_basic(auth_hdr, user, pass)            // basic auth check
auth_token_check(token, secret)                   // HMAC-SHA256 token verify
csrf_check(expected, submitted)                   // CSRF token verify
```

**HttpOnly cookie defaults.** `cookie_make` always sets `HttpOnly` (prevents
JavaScript access via `document.cookie`), `SameSite=Lax` (blocks cross-site
POST requests from carrying the cookie), and `Path=/`. There is no API to
create insecure cookies -- safe defaults are the only option.

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
joined. New kicks after `cancel()` do not start threads. Tests:
`examples/testing/cancel_policy_test.mko`.

### Optional GC vs systems crates (Done)

In `mako.toml`:

```toml
[package]
name = "my-svc"
systems = true    # ownership rules never weakened; gc ignored/forced off
# gc = true       # app crates only — ignored when systems = true
```

## Incremental build cache

Fingerprints cover full safety-relevant inputs; NLL never skipped on a partial
fingerprint. See [BUILD.md](BUILD.md).

## Compiler-enforced checks (today)

### Unhandled `Result`

A `Result` used as a bare statement is an **error**.

### Bounds checks

Debug: abort on OOB. Release: may elide (`NDEBUG`). Explicit opt-out: `unsafe`.

### Diagnostics

Lexer/parser/type errors print `file:line:col`, caret, and `help:` hints.

## What this is not

Mako does not claim "memory safe like a proof assistant." It claims **active
prevention** of the failures that hurt backends most: ignored errors, overruns,
leaked tasks, use-after-move, header/SQL injection footguns — with ownership
rules that systems crates keep even if GC is enabled elsewhere.
