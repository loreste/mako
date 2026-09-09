#!/usr/bin/env bash
# Honest Mako vs Go vs Rust microbench (ns) + optional peak RSS.
# Requires: clang (via mako), go, rustc (skips missing tools).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export MAKO_RUNTIME="$ROOT/runtime"
mkdir -p out

BIN="$(cargo metadata --format-version 1 --no-deps 2>/dev/null | python3 -c "import sys,json; print(json.load(sys.stdin)['target_directory'])")/release/mako"
if [[ ! -x "$BIN" ]]; then
  cargo build --release
  BIN="$(cargo metadata --format-version 1 --no-deps 2>/dev/null | python3 -c "import sys,json; print(json.load(sys.stdin)['target_directory'])")/release/mako"
fi

rss_of() {
  local bin="$1"
  if [[ "$(uname -s)" == "Darwin" ]]; then
    # time -l prints to stderr; maximum resident set size is in bytes
    /usr/bin/time -l "$bin" >/dev/null 2>/tmp/mako_time_rss.txt || true
    awk '/maximum resident set size/ {print $1; exit}' /tmp/mako_time_rss.txt
  else
    /usr/bin/time -v "$bin" >/dev/null 2>/tmp/mako_time_rss.txt || true
    awk '/Maximum resident set size/ {print $6; exit}' /tmp/mako_time_rss.txt
  fi
}

echo "=== Build ==="
"$BIN" build --release --no-incremental --backend c examples/bench/micro.mko -o out/bench_micro
if command -v go >/dev/null 2>&1; then
  go build -ldflags="-s -w" -o out/bench_micro_go examples/bench/micro_go.go
fi
if command -v rustc >/dev/null 2>&1; then
  rustc -C opt-level=3 -C lto -C codegen-units=1 -C strip=symbols \
    examples/bench/micro_rs.rs -o out/bench_micro_rs
fi

echo ""
echo "=== CPU (ns wall for each kernel; lower is better) ==="
echo "--- Mako ---"
./out/bench_micro
if [[ -x out/bench_micro_go ]]; then
  echo "--- Go ---"
  ./out/bench_micro_go
fi
if [[ -x out/bench_micro_rs ]]; then
  echo "--- Rust ---"
  ./out/bench_micro_rs
fi

echo ""
echo "=== Memory (peak RSS bytes on macOS / KB on Linux — see uname) ==="
echo -n "mako RSS: "
rss_of ./out/bench_micro || echo "n/a"
if [[ -x out/bench_micro_go ]]; then
  echo -n "go RSS: "
  rss_of ./out/bench_micro_go || echo "n/a"
fi
if [[ -x out/bench_micro_rs ]]; then
  echo -n "rust RSS: "
  rss_of ./out/bench_micro_rs || echo "n/a"
fi

echo ""
echo "Parse with: python3 scripts/parse_bench_ns.py  (optional)"
echo "Docs: docs/PERFORMANCE.md"
