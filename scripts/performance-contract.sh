#!/usr/bin/env bash
# Enforce the 0.5.13 performance contract that can run reproducibly in CI.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

CONTRACT="${MAKO_PERF_CONTRACT:-$ROOT/benchmarks/performance-contract.json}"
OUT_DIR="${MAKO_PERF_OUT:-$ROOT/out/perf-contract}"
mkdir -p "$OUT_DIR"

if [[ ! -f "$CONTRACT" ]]; then
  echo "performance-contract: missing $CONTRACT" >&2
  exit 2
fi

python3 - "$CONTRACT" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
data = json.loads(path.read_text(encoding="utf-8"))
if data.get("schema") != "mako.performance-contract.v1":
    raise SystemExit(f"unsupported performance contract schema in {path}")
names = [item["name"] for item in data.get("runtime_vs_rust", [])]
required = {"fib30x5", "struct1m", "slice100k", "map50k", "string20k", "chan50k"}
missing = sorted(required.difference(names))
if missing:
    raise SystemExit(f"missing runtime contract workloads: {', '.join(missing)}")
if not data.get("parser_hot_paths"):
    raise SystemExit("missing parser hot-path budgets")
if not data.get("http", {}).get("command"):
    raise SystemExit("missing HTTP benchmark method")
print(f"performance-contract: loaded {path}")
PY

"$ROOT/scripts/bench-gate.sh" 1.5

COMPILE_REPORT="$OUT_DIR/compile.json"
python3 scripts/bench-compile.py \
  --root "$OUT_DIR/compile-bench" \
  --sizes 1000 \
  --shapes single backend \
  --repeat 1 \
  --output "$COMPILE_REPORT"

python3 - "$CONTRACT" "$COMPILE_REPORT" <<'PY'
import json
import sys
from pathlib import Path

contract = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
report = json.loads(Path(sys.argv[2]).read_text(encoding="utf-8"))
budgets = {
    "single": next(
        item["max_ms"] for item in contract["parser_hot_paths"]
        if item["name"] == "single-1k-check-cold"
    ),
    "backend": next(
        item["max_ms"] for item in contract["parser_hot_paths"]
        if item["name"] == "backend-1k-check-cold"
    ),
}
failed = False
for fixture in report["fixtures"]:
    shape = fixture["shape"]
    if shape not in budgets:
        continue
    actual = float(fixture["metrics"]["check"]["cold"]["median_ms"])
    limit = float(budgets[shape])
    print(
        f"performance-contract parser {shape}-1k-check-cold: "
        f"{actual:.3f}ms (max {limit:.3f}ms)"
    )
    if actual > limit:
        failed = True
if failed:
    raise SystemExit("performance-contract: parser budget exceeded")
PY

echo "performance-contract: PASS"
