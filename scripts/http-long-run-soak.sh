#!/usr/bin/env bash
# HTTP long-run soak (docs/LONG_RUNNING.md · LR-6).
#
# Builds examples/bench/http_long_run_server.mko, drives N requests, samples
# the server process RSS while under load, and requires a clean exit.
#
# Usage:
#   ./scripts/http-long-run-soak.sh
#   MAKO_HTTP_SOAK_REQUESTS=5000 MAKO_HTTP_SOAK_PORT=19811 ./scripts/http-long-run-soak.sh
#
# Requirements: curl, python3. Optional load: wrk/hey not required.
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
mako_bin="${MAKO_BIN:-$repo_dir/target/release/mako}"
out_dir="${MAKO_HTTP_SOAK_OUT:-$repo_dir/out/http-long-run-soak}"
fixture="$repo_dir/examples/bench/http_long_run_server.mko"
requests="${MAKO_HTTP_SOAK_REQUESTS:-2000}"
port="${MAKO_HTTP_SOAK_PORT:-19810}"
# Concurrent client workers for the driver (not OS threads in the server).
clients="${MAKO_HTTP_SOAK_CLIENTS:-8}"
# Max RSS growth vs first sample while serving (1.25 = +25% under noise).
max_rss_ratio="${MAKO_HTTP_SOAK_RSS_RATIO:-1.25}"
backend="${MAKO_HTTP_SOAK_BACKEND:-c}"

if [[ ! -x "$mako_bin" ]]; then
  echo "http-long-run-soak: building release mako" >&2
  cargo build --release --manifest-path "$repo_dir/Cargo.toml"
fi
if [[ ! -f "$fixture" ]]; then
  echo "http-long-run-soak: missing $fixture" >&2
  exit 2
fi

mkdir -p "$out_dir"
bin="$out_dir/http_long_run_server"
log="$out_dir/server.log"

echo "http-long-run-soak: backend=$backend requests=$requests port=$port clients=$clients"
"$mako_bin" build "$fixture" --release --backend "$backend" --no-incremental -o "$bin"

# Free the port if a previous run left something behind.
if command -v lsof >/dev/null 2>&1; then
  lsof -tiTCP:"$port" -sTCP:LISTEN 2>/dev/null | xargs kill 2>/dev/null || true
fi

rm -f "$log"
"$bin" "$requests" "$port" >"$log" 2>&1 &
spid=$!
cleanup() {
  if kill -0 "$spid" 2>/dev/null; then
    kill "$spid" 2>/dev/null || true
    wait "$spid" 2>/dev/null || true
  fi
}
trap cleanup EXIT

# Wait for listen line
for _ in $(seq 1 100); do
  if grep -q "http_long_run on" "$log" 2>/dev/null; then
    break
  fi
  if ! kill -0 "$spid" 2>/dev/null; then
    echo "http-long-run-soak: server died before listen" >&2
    cat "$log" >&2 || true
    exit 1
  fi
  sleep 0.05
done
if ! grep -q "http_long_run on" "$log" 2>/dev/null; then
  echo "http-long-run-soak: timeout waiting for bind" >&2
  cat "$log" >&2 || true
  exit 1
fi

# Drive load + sample RSS of the server PID.
python3 - "$spid" "$port" "$requests" "$clients" "$max_rss_ratio" <<'PY'
import os, sys, time, urllib.error, urllib.request, concurrent.futures

spid = int(sys.argv[1])
port = int(sys.argv[2])
total = int(sys.argv[3])
clients = int(sys.argv[4])
max_ratio = float(sys.argv[5])
base = f"http://127.0.0.1:{port}"

def alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False

def rss_kib(pid: int) -> int:
    if not alive(pid):
        return 0
    if sys.platform == "darwin":
        import subprocess
        try:
            out = subprocess.check_output(
                ["ps", "-o", "rss=", "-p", str(pid)], text=True, stderr=subprocess.DEVNULL
            )
            return int(out.strip() or "0")
        except (subprocess.CalledProcessError, ValueError):
            return 0
    try:
        with open(f"/proc/{pid}/status") as f:
            for line in f:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1])
    except FileNotFoundError:
        return 0
    return 0

def one(i: int) -> bool:
    path = "/health" if (i % 5 == 0) else "/"
    try:
        with urllib.request.urlopen(base + path, timeout=5) as r:
            r.read()
        return True
    except (urllib.error.URLError, TimeoutError, ConnectionError, OSError):
        return False

rss_samples = []
done = 0
ok = 0
batch = max(clients * 4, 32)
t0 = time.time()
with concurrent.futures.ThreadPoolExecutor(max_workers=clients) as ex:
    while done < total and alive(spid):
        n = min(batch, total - done)
        futs = [ex.submit(one, done + k) for k in range(n)]
        for f in concurrent.futures.as_completed(futs):
            if f.result():
                ok += 1
        done += n
        sample = rss_kib(spid)
        if sample > 0:
            rss_samples.append(sample)
        if len(rss_samples) <= 3 or done >= total or not alive(spid):
            print(
                f"http-long-run-soak: progress ok={ok} attempted={done}/{total} "
                f"rss_kib={sample} server_alive={alive(spid)}"
            )

elapsed = time.time() - t0
print(
    f"http-long-run-soak: completed ok={ok} attempted={done} "
    f"in {elapsed:.2f}s ({ok/max(elapsed,1e-6):.0f} ok-req/s)"
)

# Server may exit as soon as `total` accepts finish; require most requests OK.
if ok < int(total * 0.95):
    raise SystemExit(f"http-long-run-soak: too few successful requests ({ok}/{total})")

if not rss_samples:
    raise SystemExit("http-long-run-soak: no RSS samples (server died too early?)")
base_rss = rss_samples[0]
if len(rss_samples) >= 3:
    base_rss = rss_samples[1]
worst = max(rss_samples) / max(base_rss, 1)
print(
    f"http-long-run-soak: rss base={base_rss} n_samples={len(rss_samples)} "
    f"worst_ratio={worst:.3f} (bar {max_ratio})"
)
if base_rss > 0 and worst > max_ratio:
    raise SystemExit(f"http-long-run-soak: RSS grew {worst:.3f}× under load (max {max_ratio})")
print("http-long-run-soak: RSS under load ok")
PY

# Server should exit after exactly `requests` accepts.
for _ in $(seq 1 200); do
  if ! kill -0 "$spid" 2>/dev/null; then
    break
  fi
  sleep 0.05
done
if kill -0 "$spid" 2>/dev/null; then
  echo "http-long-run-soak: server still running after load" >&2
  # Nudge remaining capacity with extra health hits (should already be done)
  exit 1
fi
wait "$spid" || true
spid=
trap - EXIT

if ! grep -q "http_long_run done" "$log"; then
  echo "http-long-run-soak: missing done line" >&2
  cat "$log" >&2 || true
  exit 1
fi

echo "http-long-run-soak: all checks passed"
