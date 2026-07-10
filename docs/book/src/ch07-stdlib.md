# 7. Standard Library

Mako’s stdlib aims at **backends**: strings, net/http, encoding,
crypto, sync, SQL clients, templates, and more. Honest coverage today is
~**98%** of major target areas (Wave 9) — not every symbol.

Call builtins directly (`str_split`, `path_join`, …) **or** import packages:

```mko
import "strings"
import "path"
import "sync"

fn main() {
    let s = strings.concat(strings.split("a,b", ","), "-")
    let p = path.clean("/a/../b")
    let m = sync.rwmutex()
    print(s)
    print(p)
}
```

Bare `import "strings"` resolves under `std/` (`MAKO_STD` override) and
auto-aliases so `strings.split` works. Method names that are keywords use
aliases (`concat` instead of `join`, `matches` instead of `match`).

## Package map (high level)

| Area | Packages (examples) |
|------|---------------------|
| Text | `strings`, `bytes`, `strconv`, `fmt`, `unicode/utf8`, `regexp` |
| Files | `io`, `fs`, `path`, `filepath`, `bufio`, `os`, `os/exec` |
| Net | `net`, `http`, `net/url`, `net/mail`, `net/smtp` |
| Encoding | `json`, `encoding/*`, `gob`, `binary`, `base64`, `csv` |
| Compress | `compress/gzip`, `archive/tar`, `archive/zip` |
| Crypto | `crypto` (hashes, AEAD when OpenSSL linked) |
| Sync | `sync`, `sync/atomic`, `context` |
| Data | `sql`, SQLite/Redis/Postgres clients |
| Image | `image/png`, `gif`, `jpeg` |
| Other | `flag`, `log`/`slog`, `html`/`text/template`, `maps`, `slices`, `reflect`, `embed` |

Authoritative tables: **[STDLIB.md](../../STDLIB.md)**. Runtime headers:
`mako_rt.h`, `mako_stdlib.h`, `mako_http.h`, `mako_db.h`, `mako_security.h`,
`mako_goext.h`.

## Idioms

- Prefer package imports for readable call sites (`strings.trim`).
- Pre-size builders and maps when you know `n`.
- Use arenas for request-scoped buffers (chapter 4).
- Treat Wave residuals honestly: complete Unicode/PCRE, Huffman JPEG for arbitrary
  viewers, live reflect field values — see STATUS.

Demo: `examples/stdlib/demo.mko`. Tests: `examples/testing/stdlib_*`,
`goext_wave*_test.mko`.

Next: [Networking & HTTP](ch08-networking.md).
