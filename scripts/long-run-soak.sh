#!/usr/bin/env bash
# Long-running steady-state soak gate (docs/LONG_RUNNING.md · LR-1).
#
# Builds examples/bench/long_run_soak.mko (release), runs it, and checks:
#   1. exit 0 and live_delta == 0 (ownership)
#   2. optional multi-sample RSS stability while re-running
#
# Usage:
#   ./scripts/long-run-soak.sh
#   MAKO_LONG_RUN_BACKEND=llvm ./scripts/long-run-soak.sh
#   MAKO_LONG_RUN_RSS_SAMPLES=5 ./scripts/long-run-soak.sh
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
mako_bin="${MAKO_BIN:-$repo_dir/target/release/mako}"
out_dir="${MAKO_LONG_RUN_OUT:-$repo_dir/out/long-run-soak}"
fixture="$repo_dir/examples/bench/long_run_soak.mko"
backend="${MAKO_LONG_RUN_BACKEND:-}"
rss_samples="${MAKO_LONG_RUN_RSS_SAMPLES:-3}"
# Max allowed RSS growth ratio across samples after the first (1.15 = +15%).
max_rss_ratio="${MAKO_LONG_RUN_RSS_RATIO:-1.15}"

if [[ ! -x "$mako_bin" ]]; then
  echo "long-run-soak: building release mako" >&2
  cargo build --release --manifest-path "$repo_dir/Cargo.toml"
fi

mkdir -p "$out_dir"
bin="$out_dir/long_run_soak"

# Prefer LLVM release when the binary supports it and the user did not pin.
if [[ -z "$backend" ]]; then
  if "$mako_bin" build --help 2>/dev/null | grep -q 'llvm'; then
    if "$mako_bin" build "$fixture" --release --backend llvm --no-incremental \
        -o "$out_dir/_llvm_probe" 2>/dev/null; then
      backend=llvm
      rm -f "$out_dir/_llvm_probe"
    else
      backend=c
    fi
  else
    backend=c
  fi
fi

echo "long-run-soak: backend=$backend"
"$mako_bin" build "$fixture" --release --backend "$backend" --no-incremental -o "$bin"

# --- Correctness + live-bytes ---
set +e
out="$("$bin" 2>&1)"
status=$?
set -e
echo "long-run-soak: output:"
echo "$out"
if [[ $status -ne 0 ]]; then
  echo "long-run-soak: process failed (exit $status) — likely live_delta != 0" >&2
  exit 1
fi
# print_int emits one integer per line: cycles, acc, live_delta, live_all
cycles="$(printf '%s\n' "$out" | grep -E '^-?[0-9]+$' | sed -n '1p')"
acc="$(printf '%s\n' "$out" | grep -E '^-?[0-9]+$' | sed -n '2p')"
live_delta="$(printf '%s\n' "$out" | grep -E '^-?[0-9]+$' | sed -n '3p')"
live_all="$(printf '%s\n' "$out" | grep -E '^-?[0-9]+$' | sed -n '4p')"
if [[ -z "$cycles" || -z "$acc" || -z "$live_delta" || -z "$live_all" ]]; then
  echo "long-run-soak: malformed output (need 4 integers)" >&2
  exit 1
fi
if [[ "$live_delta" != "0" ]]; then
  echo "long-run-soak: live_delta=$live_delta (want 0)" >&2
  exit 1
fi
echo "long-run-soak: ownership ok (cycles=$cycles acc=$acc live_all=$live_all)"

# --- RSS stability (multiple full runs; peak RSS via getrusage) ---
python3 - "$bin" "$rss_samples" "$max_rss_ratio" <<'PY'
import os, resource, statistics, subprocess, sys

bin_path, samples, max_ratio = sys.argv[1], int(sys.argv[2]), float(sys.argv[3])
if samples < 2:
    print("long-run-soak: RSS samples < 2 — skip stability")
    raise SystemExit(0)

def peak_rss_kb(path: str) -> int:
    # ru_maxrss: Linux KiB, macOS bytes — normalize to KiB.
    usage_before = resource.getrusage(resource.RUSAGE_CHILDREN)
    subprocess.run([path], check=True, stdout=subprocess.DEVNULL)
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    # Delta is unreliable for maxrss; read absolute child max when available.
    rss = usage.ru_maxrss
    if sys.platform == "darwin":
        rss = int(rss / 1024)  # bytes → KiB
    return int(rss)

rss_vals = []
for i in range(samples):
    rss_vals.append(peak_rss_kb(bin_path))
    print(f"long-run-soak: sample {i+1}/{samples} peak_rss_kib={rss_vals[-1]}")

base = rss_vals[0]
if base <= 0:
    print("long-run-soak: RSS base unavailable — skip ratio", file=sys.stderr)
    raise SystemExit(0)
worst = max(rss_vals) / base
med = statistics.median(rss_vals)
print(f"long-run-soak: rss base={base} median={med:.0f} worst_ratio={worst:.3f} (bar {max_ratio})")
if worst > max_ratio:
    raise SystemExit(
        f"long-run-soak: RSS grew {worst:.3f}× across samples (max {max_ratio})"
    )
print("long-run-soak: RSS stability ok")
PY

echo "long-run-soak: all checks passed"
