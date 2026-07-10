# 15. Appendix

## Keywords

38 reserved words from the lexer — you cannot name a variable `crew` or
`default`. Full table: **[KEYWORDS.md](../../KEYWORDS.md)**.

Not keywords (ordinary identifiers / builtins): `int`, `string`, `map`, `Ok`,
`Err`, `len`, `append`, `print`, …

Operators (`==`, `&&`, `&`, …) are not keywords; see chapter 3.

## Status snapshot

| Scope | Approx. |
|-------|---------|
| MVP / STATUS north-star | **100%** |
| Stdlib coverage major *areas* | **~98%** (Wave 9) |
| Suite | **130** passed |

True residuals: complete Unicode property database / full PCRE, interoperable
Huffman JPEG, live reflect field values, full SMTP AUTH-over-TLS, symbol-level parity.
**[STATUS.md](../../STATUS.md)**.

## Roadmap

Landed: compiler → C → native, operators & imports, `mako version`,
Waves 1–9 stdlib, CFG NLL, HTTP beachhead, WASI preview1.

Next / Later: complete Unicode property database / PCRE depth, optional GC, SIMD/GPU, deep LSP, WASI
preview2, homebrew-core publish (external).
**[ROADMAP.md](../../ROADMAP.md)** · **[VISION.md](../../VISION.md)**.

## Doc map

| Doc | Role |
|-----|------|
| [The Mako Book](ch00-introduction.md) | This guided tour |
| [GUIDE.md](../../GUIDE.md) | Exhaustive verified syntax |
| [LANGUAGE.md](../../LANGUAGE.md) | Design overview |
| [STDLIB.md](../../STDLIB.md) | Package surface |
| [howto/](../../howto/) | Recipes |
| [CHANGELOG.md](../../../CHANGELOG.md) | Release notes |

## Checkable book examples

| File | Topic |
|------|--------|
| [`../examples/book_hello.mko`](../examples/book_hello.mko) | Hello + fib |
| [`../examples/book_ops.mko`](../examples/book_ops.mko) | Operators |
| [`../examples/book_errors.mko`](../examples/book_errors.mko) | Result wrapping |
| [`../examples/book_imports.mko`](../examples/book_imports.mko) | Grouped imports |

```bash
mako check docs/book/examples/*.mko
mako run docs/book/examples/book_hello.mko
```
