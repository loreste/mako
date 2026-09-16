#!/usr/bin/env bash
# Matched Rust/Mako MPSC and sharded actor measurements; no scheduler-noise gate.
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
compiler="${MAKO_BENCH_COMPILER:-$repo_dir/target/release/mako}"
export MAKO_RUNTIME="$repo_dir/runtime"
export MAKO_STD="$repo_dir/std"
samples="${MAKO_BENCH_SAMPLES:-5}"
output_dir="$(mktemp -d "${TMPDIR:-/tmp}/mako-actor-scaling.XXXXXX")"
trap 'rm -rf "$output_dir"' EXIT
"$compiler" build --release --no-incremental --backend c \
  "$repo_dir/examples/bench/actor_scaling.mko" -o "$output_dir/mako"
rustc -C opt-level=3 -C lto -C codegen-units=1 \
  "$repo_dir/examples/bench/actor_scaling_rs.rs" -o "$output_dir/rust"
python3 - "$output_dir" "$samples" <<'PY'
import pathlib, statistics, subprocess, sys
root, samples = pathlib.Path(sys.argv[1]), int(sys.argv[2])
if samples < 1:
    raise SystemExit("MAKO_BENCH_SAMPLES must be positive")
def run(lang):
    lines = subprocess.check_output([str(root / lang)], text=True, timeout=120).splitlines()
    values = {}
    for i in range(0, len(lines), 5):
        if lines[i] != "actor_scaling" or i + 4 >= len(lines):
            raise SystemExit(f"Invalid {lang} benchmark output")
        key = tuple(map(int, lines[i + 1:i + 4]))
        values[key] = int(lines[i + 4])
    return values
expected = {(p, cap, 1) for p in (1, 2, 4, 8) for cap in (64, 1024)}
expected |= {(1, 1024, s) for s in (2, 4, 8)}
measurements = {lang: {key: [] for key in expected} for lang in ("mako", "rust")}
for lang in measurements:
    if set(run(lang)) != expected:
        raise SystemExit(f"Missing {lang} benchmark scenarios")
for sample in range(samples):
    for lang in (("mako", "rust") if sample % 2 == 0 else ("rust", "mako")):
        values = run(lang)
        if set(values) != expected:
            raise SystemExit(f"Missing {lang} benchmark scenarios")
        for key, elapsed in values.items():
            measurements[lang][key].append(elapsed)
print("producers capacity shards mako_ms rust_ms mako/rust mako_Mmsg/s")
for key in sorted(expected, key=lambda k: (k[2], k[1], k[0])):
    mako, rust = (statistics.median(measurements[lang][key]) for lang in ("mako", "rust"))
    print(*key, f"{mako / 1e6:.2f}", f"{rust / 1e6:.2f}", f"{mako / rust:.2f}x", f"{200_000 / mako * 1e3:.2f}")
PY
