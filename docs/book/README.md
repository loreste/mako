# The Mako Book

A guided tour of Mako: install, language, ownership, concurrency, stdlib, HTTP, packages, and tooling.

**Product tip:** **0.4.0** (native compilation, ownership, concurrency, and
stdlib surface). Next planned: **0.4.0** performance and stdlib depth — [ROADMAP.md](../ROADMAP.md).

| | |
|--|--|
| **Start here** | [src/ch01-preface.md](src/ch01-preface.md) |
| **Table of contents** | [src/SUMMARY.md](src/SUMMARY.md) |
| **Checkable samples** | [examples/](../../examples/) |

## Read as Markdown

Open chapters under `src/` in any editor, or follow links from the [main README](../../README.md) and [GUIDE](../GUIDE.md).

## Build with mdBook (optional)

```bash
# Install: cargo install mdbook
cd docs/book
mdbook serve --open    # live preview
mdbook build           # → book/ (HTML)
```

`book.toml` is ready; mdBook is not required to read the source chapters.

## Related docs

| Doc | Role |
|-----|------|
| [GUIDE.md](../GUIDE.md) | Exhaustive verified syntax |
| [STDLIB.md](../STDLIB.md) | Package surface |
| [STATUS.md](../STATUS.md) | Adversarial Done matrix |
| [howto/](../howto/) | Task-oriented recipes |
