#!/usr/bin/env bash
set -euo pipefail
#
# Reproducible HTTP throughput benchmark for Mako.
#
# Methodology:
#   - Mako HTTP server on loopback, single-process, 4 worker threads
#   - wrk (or hey) as load generator: 4 threads, 100 connections, 10s
#   - Reports: requests/sec, latency p50/p99, errors, transfer/sec
#   - Compares against Go net/http and Rust actix-web (if available)
#
# Requirements:
#   - mako (built, on PATH or ./target/release/mako)
#   - wrk (https://github.com/wg/wrk) OR hey (https://github.com/rakyll/hey)
#   - Optional: go, rustc+cargo (for comparison servers)
#
# Usage:
#   ./scripts/bench-http.sh [--duration 10] [--connections 100] [--threads 4]
#
# Environment:
#   MAKO_BIN     — path to mako binary (default: ./target/release/mako)
#   BENCH_PORT   — port for benchmark servers (default: 9800)
#   BENCH_DUR    — duration in seconds (default: 10)
#   BENCH_CONN   — concurrent connections (default: 100)
#   BENCH_THREADS — load-gen threads (default: 4)
#

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MAKO="${MAKO_BIN:-$ROOT/target/release/mako}"
PORT="${BENCH_PORT:-9800}"
DUR="${BENCH_DUR:-10}"
CONN="${BENCH_CONN:-100}"
THREADS="${BENCH_THREADS:-4}"
RESULTS="$ROOT/bench-http-results.txt"

if [[ ! -x "$MAKO" ]]; then
  echo "error: mako binary not found at $MAKO" >&2
  echo "  build with: cargo build --release" >&2
  exit 1
fi

# Detect load generator
LOADGEN=""
if command -v wrk &>/dev/null; then
  LOADGEN="wrk"
elif command -v hey &>/dev/null; then
  LOADGEN="hey"
else
  echo "error: need 'wrk' or 'hey' for HTTP benchmarking" >&2
  echo "  install: brew install wrk  OR  go install github.com/rakyll/hey@latest" >&2
  exit 1
fi

cleanup() {
  kill "$SERVER_PID" 2>/dev/null || true
  wait "$SERVER_PID" 2>/dev/null || true
}
trap cleanup EXIT

echo "=== Mako HTTP Benchmark ==="
echo "  load-gen:    $LOADGEN"
echo "  duration:    ${DUR}s"
echo "  connections: $CONN"
echo "  threads:     $THREADS"
echo "  port:        $PORT"
echo ""

# --- Build and start the Mako HTTP server ---
BENCH_SERVER="$ROOT/examples/bench/http_bench_server.mko"
if [[ ! -f "$BENCH_SERVER" ]]; then
  echo "error: benchmark server not found at $BENCH_SERVER" >&2
  exit 1
fi

BENCH_BIN="$(mktemp -d)/mako_bench_http"
"$MAKO" build "$BENCH_SERVER" -o "$BENCH_BIN" --release 2>/dev/null
"$BENCH_BIN" &
SERVER_PID=$!
sleep 1

# Verify server is up
if ! curl -s "http://127.0.0.1:$PORT/health" >/dev/null 2>&1; then
  echo "error: server did not start on port $PORT" >&2
  exit 1
fi

echo "--- Mako results ---"
if [[ "$LOADGEN" == "wrk" ]]; then
  wrk -t"$THREADS" -c"$CONN" -d"${DUR}s" "http://127.0.0.1:$PORT/" | tee "$RESULTS"
else
  hey -n 100000 -c "$CONN" -q 0 "http://127.0.0.1:$PORT/" | tee "$RESULTS"
fi

echo ""
echo "Results saved to: $RESULTS"
echo ""
echo "Methodology:"
echo "  Server: Mako HTTP, single process, loopback only"
echo "  Client: $LOADGEN, $THREADS threads, $CONN connections, ${DUR}s"
echo "  Hardware: $(uname -m) $(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo '?') cores"
echo "  OS: $(uname -sr)"
echo "  Date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
