# Colored async / await (Vision Later — rejected by design)

**Product tip:** **0.2.0** — concurrency remains colorless (`crew` / channels).

Mako concurrency is **colorless**: `crew` / `kick` / `join` / channels / `select`.

The keywords `async` and `await` are **rejected at lex time** with a clear error pointing
at crew/kick/channels. There is no dual-colored function coloring and no
`Future`-returning async fn ABI.

This is intentional (see [VISION.md](VISION.md) / [LANGUAGE.md](LANGUAGE.md)): one
calling convention, structured concurrency, no function-color split.

```mko
// Rejected:
// async fn f() { await g() }

// Idiomatic:
crew c {
    let j = c.kick(work())
    let v = j.join()
}
```
