# Compiler scaling fixtures

These fixtures measure how the Mako frontend scales with source size and
project shape. They are generated rather than committed as large source trees,
so every run starts from the same inputs without adding hundreds of thousands
of generated lines to the repository.

The default matrix covers 1k, 10k, and 100k lines for:

- `single`: one source file with a long call graph;
- `multi`: the same style of program spread across package files;
- `generic`: generic declarations and instantiations;
- `backend`: structs, JSON decoding, results, matches, and route dispatch.

Generate the full matrix without measuring it:

```bash
python3 scripts/bench-compile.py --generate-only
```

Measure cold and cached `mako check` times:

```bash
cargo build --release
python3 scripts/bench-compile.py --output out/compile-bench.json
```

Add `--build` to measure full debug builds as well. Full builds require the
configured C compiler and are intentionally opt-in because the 100k fixtures
can take much longer than frontend-only checks.

Use `--sizes` or `--shapes` for focused runs:

```bash
python3 scripts/bench-compile.py --sizes 1000 10000 --shapes multi generic
```

The runner validates the exact `.mko` line count before every measurement and
uses a separate incremental cache for each fixture. A cold check explicitly
disables Mako's incremental cache; it does not attempt to flush operating-system
file caches. Compiler commands time out after five minutes. The runner records
raw samples, medians, host details, compiler version, and source commit in the
`mako.compile-bench.v1` JSON schema. These results are evidence for local
comparison, not stable performance claims or CI thresholds.
