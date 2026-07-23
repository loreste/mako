# Mako versioning

**Product string:** `mako` + `CARGO_PKG_VERSION` (e.g. `mako0.4.7`).  
**Git tags:** `vMAJOR.MINOR.PATCH` matching `Cargo.toml`.

## Policy (from 0.4.6+)

Ship **small, measurable increments**. Prefer a **patch** over waiting for a large minor.

| Kind | When | Examples |
|------|------|----------|
| **PATCH** `0.4.N` | Residual perf, gates, docs, packaging seeds, env knobs, CI honesty, bugfixes | immortal strings, dead_strip, `MAKO_BACKEND`, bench bar tweak |
| **MINOR** `0.N.0` | Theme shift with a clear product story | native-first **default**, IDE product depth, runtime trust soaks |
| **MAJOR** `1.0.0` | Stability / compat contract | LTS-ish freeze after 0.5 series |

### Rules

1. **Every tag has a CHANGELOG section** with honest gates (not marketing).
2. **Do not skip patch numbers** on the tip train (`0.4.5` → `0.4.6` → `0.4.7` …). Historical gaps (e.g. 0.4.1 → 0.4.5) stay as history.
3. **One theme per release** when possible. Split “native residual + policy + IDE” into three patches/minors rather than one mega-cut.
4. **Default CLI behavior** changes only on a minor (or major), never a silent patch.
5. **Packaging** (Homebrew/winget/Formula SHAs) updates **after** the GitHub Release for that tag — not blocked by the next theme.
6. **Unreleased** work on `main` may sit as `Unreleased → next PATCH` until cut; bump `Cargo.toml` when the ship set is named.

### Cadence (guidance, not a calendar)

- Patches: as soon as a residual bar or doc/env story is green (days–weeks).
- Minors: when a north-star checklist exits (weeks–months).
- Do not batch months of work under “Unreleased” waiting for 0.5.0.

## Current train

See [ROADMAP.md](ROADMAP.md) version map. Tip after `v0.4.5`:

```text
0.4.6  residual perf + binary size + honest bench bars + backend policy env
0.4.7  modes truth table (fail closed)
0.4.8  map/I/O bench gates + perf regression budget
0.4.9  LLVM CI (macOS) + install/doctor smoke
0.4.10 years-up soak foundation
0.4.11 HTTP soak + allocator/PGO product  (tip)
0.5.0  native-first default (CLI default flip — minor)
0.5.1  toolchain / IDE depth
0.5.2  runtime trust soaks
0.5.x  further patches as needed
1.0    stability freeze
```

Adjust PATCH themes if a smaller ship is ready earlier — renumber forward, never rewrite shipped tags.
