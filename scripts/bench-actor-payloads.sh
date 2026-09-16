#!/usr/bin/env bash
# Compare the same typed-message workload across compiler revisions.
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
export MAKO_STD="$repo_dir/std"
export MAKO_RUNTIME="$repo_dir/runtime"
compiler="${MAKO_BENCH_COMPILER:-$repo_dir/target/release/mako}"
samples="${MAKO_BENCH_SAMPLES:-5}"
output_dir="$(mktemp -d "${TMPDIR:-/tmp}/mako-actor-payloads.XXXXXX")"
trap 'rm -rf "$output_dir"' EXIT
"$compiler" build --release --no-incremental --backend c \
  "$repo_dir/examples/bench/actor_payloads.mko" -o "$output_dir/current"
if [[ -n "${MAKO_BENCH_BASELINE:-}" ]]; then
  "$MAKO_BENCH_BASELINE" build --release --no-incremental --backend c \
    "$repo_dir/examples/bench/actor_payloads.mko" -o "$output_dir/baseline"
fi
python3 - "$output_dir" "$samples" <<'PY'
import pathlib, statistics, subprocess, sys
root, samples = pathlib.Path(sys.argv[1]), int(sys.argv[2])
if samples < 1:
    raise SystemExit("MAKO_BENCH_SAMPLES must be positive")
names = ["current"] + (["baseline"] if (root / "baseline").exists() else [])
measurements = {name: [] for name in names}
for sample in range(samples):
    for name in names if sample % 2 == 0 else list(reversed(names)):
        values = list(map(int, subprocess.check_output([str(root / name)], text=True, timeout=120).splitlines()))
        if len(values) != 1003 or any(v < 0 for v in values):
            raise SystemExit(f"Invalid benchmark output from {name}")
        measurements[name].append(values)
print("compiler pair200k_ms bool200k_ms fresh_slice20k_ms latency_p50_us latency_p95_us latency_p99_us")
for name, runs in measurements.items():
    times = [statistics.median(run[i] for run in runs) / 1e6 for i in range(3)]
    latencies = sorted(v for run in runs for v in run[3:])
    quantiles = [latencies[min(len(latencies) - 1, int(len(latencies) * q))] / 1000 for q in (.5, .95, .99)]
    print(name, *(f"{v:.2f}" for v in times + quantiles))
PY
