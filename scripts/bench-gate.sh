#!/usr/bin/env bash
# Gate: Mako release microbench must stay within a factor of Rust.
# Speed bar: as close to Rust as possible; claims are workload-specific.
#
# Usage:
#   ./scripts/bench-gate.sh           # contract budgets (default max 2.0×)
#   ./scripts/bench-gate.sh 1.5       # strict Rust gate cap
#   MAKO_BENCH_STRICT=1 ./scripts/bench-gate.sh  # same as 1.5
# Checks fib30x5, struct1m, slice100k, map50k, string20k, chan50k.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export MAKO_RUNTIME="$ROOT/runtime"
cd "$ROOT"
if [[ -n "${1:-}" ]]; then
  MAX_RATIO="$1"
elif [[ "${MAKO_BENCH_STRICT:-}" == "1" ]]; then
  MAX_RATIO="1.5"
else
  MAX_RATIO="2.0"
fi
CONTRACT="${MAKO_PERF_CONTRACT:-$ROOT/benchmarks/performance-contract.json}"
SAMPLES="${MAKO_BENCH_SAMPLES:-5}"

if ! command -v rustc >/dev/null 2>&1; then
  echo "bench-gate: rustc not found; strict Rust comparison cannot run" >&2
  exit 2
fi

BIN="$(cargo metadata --format-version 1 --no-deps 2>/dev/null | python3 -c "import sys,json; print(json.load(sys.stdin)['target_directory'])")/release/mako"
if [[ ! -x "$BIN" ]]; then
  cargo build --release
  BIN="$(cargo metadata --format-version 1 --no-deps 2>/dev/null | python3 -c "import sys,json; print(json.load(sys.stdin)['target_directory'])")/release/mako"
fi

mkdir -p out
"$BIN" build --release --no-incremental --backend c examples/bench/micro.mko -o out/bench_micro_gate
rustc -C opt-level=3 -C lto -C codegen-units=1 \
  examples/bench/micro_rs.rs -o out/bench_micro_rs_gate 2>/dev/null

sample_dir="$(mktemp -d "${TMPDIR:-/tmp}/mako-bench-gate.XXXXXX")"
workload_file=""
trap 'rm -rf "$sample_dir" ${workload_file:+"$workload_file"}' EXIT

if ! python3 - "$SAMPLES" <<'PY'
import sys
if int(sys.argv[1]) < 1:
    raise SystemExit(1)
PY
then
  echo "bench-gate: MAKO_BENCH_SAMPLES must be >= 1" >&2
  exit 2
fi

./out/bench_micro_gate >/dev/null
./out/bench_micro_rs_gate >/dev/null
for i in $(seq 1 "$SAMPLES"); do
  if (( i % 2 == 0 )); then
    ./out/bench_micro_rs_gate >"$sample_dir/rust-$i.out"
    ./out/bench_micro_gate >"$sample_dir/mako-$i.out"
  else
    ./out/bench_micro_gate >"$sample_dir/mako-$i.out"
    ./out/bench_micro_rs_gate >"$sample_dir/rust-$i.out"
  fi
done

workload_file="$(mktemp "${TMPDIR:-/tmp}/mako-bench-workloads.XXXXXX")"
python3 - "$CONTRACT" "$MAX_RATIO" >"$workload_file" <<'PY'
import json
import sys
from pathlib import Path

contract = Path(sys.argv[1])
cap = float(sys.argv[2])
if not contract.is_file():
    for key in ("fib30x5", "struct1m", "slice100k", "map50k", "string20k", "chan50k"):
        print(f"{key}\t{cap}")
    raise SystemExit(0)

data = json.loads(contract.read_text(encoding="utf-8"))
if data.get("schema") != "mako.performance-contract.v1":
    raise SystemExit(f"bench-gate: unsupported performance contract schema in {contract}")

for item in data.get("runtime_vs_rust", []):
    key = item["name"]
    ratio = float(item["max_ratio_vs_rust"])
    if item.get("strict_rust_claim", False):
        ratio = min(ratio, cap)
    print(f"{key}\t{ratio}")
PY

if ! python3 - "$sample_dir" "$workload_file" "$SAMPLES" <<'PY'
import statistics
import sys
from pathlib import Path

sample_dir = Path(sys.argv[1])
workload_file = Path(sys.argv[2])
samples = int(sys.argv[3])

def parse(path):
    lines = path.read_text(encoding="utf-8").splitlines()
    values = {}
    for index, line in enumerate(lines):
        if index + 2 < len(lines):
            try:
                values[line] = int(lines[index + 2])
            except ValueError:
                pass
    return values

budgets = []
for line in workload_file.read_text(encoding="utf-8").splitlines():
    if not line:
        continue
    key, max_ratio = line.split("\t", 1)
    budgets.append((key, float(max_ratio)))

failed = False
for key, max_ratio in budgets:
    mako_values = []
    rust_values = []
    for i in range(1, samples + 1):
        mako = parse(sample_dir / f"mako-{i}.out").get(key)
        rust = parse(sample_dir / f"rust-{i}.out").get(key)
        if mako is None or rust is None:
            print(f"bench-gate: could not parse {key} sample {i}")
            failed = True
            continue
        mako_values.append(mako)
        rust_values.append(rust)
    if not mako_values or not rust_values:
        failed = True
        continue
    mako_median = statistics.median(mako_values)
    rust_median = statistics.median(rust_values)
    if rust_median <= 0:
        print(f"bench-gate {key}: invalid rust ns {rust_median}")
        failed = True
        continue
    ratio = mako_median / rust_median
    print(
        f"bench-gate {key}: mako={mako_median:.0f}ns "
        f"rust={rust_median:.0f}ns ratio={ratio:.2f}x "
        f"(max {max_ratio}x, n={samples})"
    )
    if ratio > max_ratio:
        print(
            f"FAIL: {key} — Mako is {ratio:.2f}x slower than Rust "
            f"(limit {max_ratio}x)"
        )
        failed = True

if failed:
    raise SystemExit(1)
PY
then
  echo "bench-gate: FAILED"
  exit 1
fi
echo "PASS: all kernels within performance contract (strict claims capped at ≤${MAX_RATIO}× Rust)"
