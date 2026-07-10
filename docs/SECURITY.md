# Mako security

Mako treats safety as a **compiler and runtime contract**, not a style guide.
The goal: make leaks, memory corruption, and common backend footguns hard to
ship — by construction where possible, by hard errors where not.

Guided tour: [The Mako Book §11](book/src/ch11-speed-safety.md).

## Principles

1. **Prevent, don't advise** — illegal states should not compile or should abort
   with a clear diagnostic.
2. **No mandatory GC** — backends and systems code stay on ownership + arenas
   for predictable latency. An **optional** GC mode may exist for application
   crates only; **`[package] systems = true` forbids GC weakening** of
   hold/share/move rules.
3. **Secure defaults in the stdlib** — parameterized DB APIs, header validation,
   constant-time token compare, zero-on-drop secrets.

## Memory and resources

| Risk | Mako prevention |
|------|-----------------|
| Leaks (scoped work) | Values and `arena` regions are released at scope end |
| Orphan threads | `crew` **cancel_joins** all kicked jobs on exit |
| Buffer overflow | Array/string index bounds-checked in debug; `unsafe` / `unsafe_index` opt-out |
| Use-after-move | CFG NLL + `hold` move checker (`use of moved value`) |
| Secrets in memory | `secret_from_str` / `secret_drop` — wipe via `mako_secure_zero` |
| Raw pointer games | Not available in safe Mako |

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
