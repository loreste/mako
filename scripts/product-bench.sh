#!/usr/bin/env sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT"

mkdir -p out

echo "[product-bench] compile latency"
python3 scripts/bench-compile.py --output out/compile-bench.json

echo "[product-bench] native workload gate"
./scripts/native-bench-gate.sh

echo "[product-bench] C/Rust parity gate"
./scripts/bench-gate.sh

if command -v wrk >/dev/null 2>&1 || command -v hey >/dev/null 2>&1; then
    echo "[product-bench] HTTP throughput"
    ./scripts/bench-http.sh
else
    echo "[product-bench] skip HTTP throughput: install wrk or hey"
fi

echo "[product-bench] done"
