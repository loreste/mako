# Mako status (adversarial / verified)

Last inventory: 2026-07-10 (**Wave 9** · suite **130** · ~**98%** standard library coverage · **The Mako Book**).

**Book:** [The Mako Book](book/) · **Guide:** [GUIDE.md](GUIDE.md) · **Build:** [BUILD.md](BUILD.md) · **Stdlib:** [STDLIB.md](STDLIB.md) · **Roadmap:** [ROADMAP.md](ROADMAP.md) · **Changelog:** [../CHANGELOG.md](../CHANGELOG.md).

---

## Completion estimate (honest)

| Scope | Approx. |
|-------|---------|
| **MVP / usable language** | **100%** |
| **STATUS north-star** | **100%** |
| **Standard library** | **~98%** of target areas Done (Wave 9; not every symbol) |

---

## Docs — **Done**

| Piece | Status |
|-------|--------|
| **The Mako Book** (`docs/book/` · mdBook `book.toml` + chapters) | Done |
| Accuracy pass: README / GUIDE / STATUS / ROADMAP / howto index | Done |

## Tooling — **Done**

| Piece | Status |
|-------|--------|
| `mako version` / `--version` with OS/arch | Done |
| Grouped `import (` / `{` + fmt | Done |
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
| `cargo build --release` | PASS (prior) |
| Book samples `mako check` / `run` | PASS — `docs/book/examples/book_*.mko` |
| `mako test examples/testing` | PASS — **130 passed**, 0 failed |
| The Mako Book + docs accuracy | Done |

---

## True hard residuals

1. Complete Unicode property database / full PCRE  
2. Huffman JPEG bitstream readable by arbitrary viewers (JFIF headers + MAKOJPG payload today)  
3. Reflect field *values* from live Mako structs (schema registry Done; bag still stringly)  
4. Full SMTP AUTH over a real TLS session (STARTTLS negotiate + OpenSSL upgrade path soft)  
5. Symbol-level parity inside Done packages  

---

## External

homebrew-core publish — [Formula/mako.rb](../Formula/mako.rb).
