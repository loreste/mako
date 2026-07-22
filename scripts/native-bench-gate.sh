#!/usr/bin/env bash
# Direct-native runtime gate against the C backend, hand-written C, and Rust.
# Usage: ./scripts/native-bench-gate.sh [max-native/baseline-ratio]
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
# Runtime wall-time bar vs C backend / hand C / Rust. 1.25× is the published
# residual ship bar; tighten with MAKO_NATIVE_MAX_RATIO / $1 as the tree improves.
max_ratio="${1:-${MAKO_NATIVE_MAX_RATIO:-1.25}}"
max_compile_ratio="${MAKO_NATIVE_COMPILE_RATIO:-1.00}"
max_compiler_rss_ratio="${MAKO_NATIVE_COMPILER_RSS_RATIO:-1.00}"
# Slice fill uses a larger header than bare int buffers; allow modest RSS headroom.
max_runtime_rss_ratio="${MAKO_NATIVE_RUNTIME_RSS_RATIO:-1.25}"
# ~1.01× slim hand-C is the residual floor after dead_strip (a few hundred
# bytes of runtime entrypoints); allow a little headroom for host variance.
max_binary_ratio="${MAKO_NATIVE_BINARY_RATIO:-1.05}"
samples="${MAKO_NATIVE_BENCH_SAMPLES:-7}"
warmups="${MAKO_NATIVE_BENCH_WARMUPS:-2}"
out_dir="${MAKO_NATIVE_BENCH_OUT:-$repo_dir/out/native-bench}"
mako_bin="$repo_dir/target/release/mako"

if ! command -v clang >/dev/null 2>&1; then
  echo "native bench: clang is required for the C baselines" >&2
  exit 2
fi
if ! command -v rustc >/dev/null 2>&1; then
  echo "native bench: rustc is required for the Rust baseline" >&2
  exit 2
fi
# macOS: ensure a real SDK so hand-C and the Mako C backend can find headers.
if [[ "$(uname -s)" == "Darwin" && -z "${SDKROOT:-}" ]]; then
  if command -v xcrun >/dev/null 2>&1; then
    export SDKROOT="$(xcrun --show-sdk-path 2>/dev/null || true)"
  fi
fi
if [[ "$(uname -s)" == "Darwin" && -n "${SDKROOT:-}" ]]; then
  export CFLAGS="${CFLAGS:-} -isysroot ${SDKROOT}"
  export CPATH="${SDKROOT}/usr/include${CPATH:+:$CPATH}"
fi

# Prefer llvm-backend + bundled lld when the static toolchain is present
# (same discovery as scripts/native-compiler-test.sh).
cargo_features=()
if [[ "$(uname -s)" == "Darwin" ]]; then
  if [[ -z "${LLVM_SYS_211_PREFIX:-}" ]]; then
    for candidate in /opt/homebrew/opt/llvm@21 /usr/local/opt/llvm@21; do
      if [[ -x "$candidate/bin/llvm-config" ]]; then
        export LLVM_SYS_211_PREFIX="$candidate"
        break
      fi
    done
  fi
  toolchain_dir="${MAKO_NATIVE_TOOLCHAIN_DIR:-$repo_dir/out/native-toolchain}"
  export MAKO_LLD_STATIC_DIR="${MAKO_LLD_STATIC_DIR:-$toolchain_dir/lld-build/lib}"
  if [[ ! -f "$MAKO_LLD_STATIC_DIR/liblldMachO.a" ]]; then
    for candidate in \
      /private/tmp/mako-native-toolchain-smoke/lld-build/lib \
      /private/tmp/mako-lld-static-build/lib; do
      if [[ -f "$candidate/liblldMachO.a" ]]; then
        export MAKO_LLD_STATIC_DIR="$candidate"
        break
      fi
    done
  fi
  export MAKO_LLD_INCLUDE_DIR="${MAKO_LLD_INCLUDE_DIR:-$toolchain_dir/llvm-project-llvmorg-21.1.8/lld/include}"
  if [[ ! -f "$MAKO_LLD_INCLUDE_DIR/lld/Common/Driver.h" ]]; then
    for candidate in \
      /private/tmp/mako-native-toolchain-smoke/llvm-project-llvmorg-21.1.8/lld/include; do
      if [[ -f "$candidate/lld/Common/Driver.h" ]]; then
        export MAKO_LLD_INCLUDE_DIR="$candidate"
        break
      fi
    done
  fi
  if [[ -n "${LLVM_SYS_211_PREFIX:-}" && -f "$MAKO_LLD_STATIC_DIR/liblldMachO.a" && \
        -f "$MAKO_LLD_INCLUDE_DIR/lld/Common/Driver.h" ]]; then
    cargo_features=(--features llvm-backend)
  fi
fi

cargo build --release --manifest-path "$repo_dir/Cargo.toml" "${cargo_features[@]}"
mkdir -p "$out_dir"

# Release runtime speed uses LLVM when the feature is built in; Cranelift remains
# the debug/`--backend native` path. Runtime gates compare the optimizing
# backend against C and Rust.
native_backend=native
if "$mako_bin" build --help 2>/dev/null | grep -q 'llvm'; then
  # Prefer LLVM for release runtime when available (single-file mako with llvm-backend).
  if "$mako_bin" build examples/bench/native_fib.mko --release --backend llvm --no-incremental \
      -o "$out_dir/_llvm_probe" 2>/dev/null; then
    native_backend=llvm
    rm -f "$out_dir/_llvm_probe"
  fi
fi
echo "native bench: release runtime backend=$native_backend"

workloads=(native_parity native_fib native_slice native_string_slice)
for workload in "${workloads[@]}"; do
  source_mako="$repo_dir/examples/bench/$workload.mko"
  "$mako_bin" build "$source_mako" --release --backend "$native_backend" --no-incremental \
    -o "$out_dir/$workload-native"
  "$mako_bin" build "$source_mako" --release --backend c --no-incremental \
    -o "$out_dir/$workload-mako-c"
  clang -O3 -flto "$repo_dir/examples/bench/$workload.c" \
    -o "$out_dir/$workload-hand-c"
  rustc -C opt-level=3 -C lto -C codegen-units=1 \
    "$repo_dir/examples/bench/$workload.rs" -o "$out_dir/$workload-rust"

  "$out_dir/$workload-native" >"$out_dir/$workload-native.out"
  for candidate in mako-c hand-c rust; do
    "$out_dir/$workload-$candidate" >"$out_dir/$workload-$candidate.out"
    if ! cmp -s "$out_dir/$workload-native.out" "$out_dir/$workload-$candidate.out"; then
      echo "native bench: $workload output mismatch for $candidate" >&2
      diff -u "$out_dir/$workload-native.out" \
        "$out_dir/$workload-$candidate.out" >&2 || true
      exit 1
    fi
  done
done

set +e
python3 - "$samples" "$warmups" "$max_ratio" "$max_compile_ratio" \
  "$max_compiler_rss_ratio" "$max_runtime_rss_ratio" "$max_binary_ratio" \
  "$mako_bin" "$repo_dir" "$out_dir" "${workloads[@]}" <<'PY'
import os
import resource
import statistics
import subprocess
import sys
import time

samples = int(sys.argv[1])
warmups = int(sys.argv[2])
limit = float(sys.argv[3])
compile_limit = float(sys.argv[4])
rss_limit = float(sys.argv[5])
runtime_rss_limit = float(sys.argv[6])
binary_limit = float(sys.argv[7])
mako = sys.argv[8]
repo = sys.argv[9]
out_dir = sys.argv[10]
workloads = sys.argv[11:]
names = ["mako-native", "mako-c", "hand-c", "rust"]

def run_measured(path):
    start = time.perf_counter_ns()
    pid = os.fork()
    if pid == 0:
        devnull = os.open(os.devnull, os.O_WRONLY)
        os.dup2(devnull, 1)
        os.execv(path, [path])
    _, status, usage = os.wait4(pid, 0)
    if status != 0:
        raise SystemExit(f"native bench: execution failed for {path}")
    cpu_ns = int((usage.ru_utime + usage.ru_stime) * 1_000_000_000)
    return time.perf_counter_ns() - start, usage.ru_maxrss, cpu_ns

failed = False
for workload in workloads:
    paths = [os.path.join(out_dir, f"{workload}-{suffix}")
             for suffix in ("native", "mako-c", "hand-c", "rust")]
    times = {name: [] for name in names}
    runtime_rss = {name: [] for name in names}
    cpu_time = {name: [] for name in names}
    for path in paths:
        for _ in range(warmups):
            subprocess.run([path], stdout=subprocess.DEVNULL, check=True)
    for round_index in range(samples):
        order = list(range(len(paths)))
        order = order[round_index % len(order):] + order[:round_index % len(order)]
        for index in order:
            elapsed, max_rss, cpu_ns = run_measured(paths[index])
            times[names[index]].append(elapsed)
            runtime_rss[names[index]].append(max_rss)
            cpu_time[names[index]].append(cpu_ns)
    medians = {name: statistics.median(values) for name, values in times.items()}
    for name in names:
        values_ms = [value / 1_000_000 for value in times[name]]
        print(f"native bench {workload} {name}: "
              f"median={medians[name] / 1_000_000:.3f}ms "
              f"min={min(values_ms):.3f}ms max={max(values_ms):.3f}ms "
                     f"max-rss={statistics.median(runtime_rss[name]):.0f} "
                     f"cpu-ms={statistics.median(cpu_time[name]) / 1_000_000:.3f} n={samples}")
    for baseline in names[1:]:
        ratio = medians["mako-native"] / medians[baseline]
        print(f"native bench {workload} ratio vs {baseline}: "
              f"{ratio:.3f}x (max {limit:.3f}x)")
        failed |= ratio > limit
    if workload == "native_slice":
        native_rss = statistics.median(runtime_rss["mako-native"])
        for baseline in names[1:]:
            ratio = native_rss / statistics.median(runtime_rss[baseline])
            print(f"native bench {workload} RSS ratio vs {baseline}: "
                  f"{ratio:.3f}x (max {runtime_rss_limit:.3f}x)")
            failed |= ratio > runtime_rss_limit

    native_size = os.path.getsize(paths[0])
    for index in (1, 2):
        ratio = native_size / os.path.getsize(paths[index])
        print(f"native bench {workload} binary ratio vs {names[index]}: "
              f"{ratio:.3f}x (max {binary_limit:.3f}x)")
        failed |= ratio > binary_limit

compile_source = os.path.join(repo, "examples/bench/native_parity.mko")
compile_stats = {backend: [] for backend in ("native", "c")}
rss_stats = {backend: [] for backend in ("native", "c")}
for round_index in range(samples):
    for backend in (("native", "c") if round_index % 2 == 0 else ("c", "native")):
        output = os.path.join(out_dir, f"compile-{backend}")
        argv = [mako, "build", compile_source, "--release", "--backend", backend,
                "--no-incremental", "-o", output]
        start = time.perf_counter_ns()
        pid = os.fork()
        if pid == 0:
            devnull = os.open(os.devnull, os.O_WRONLY)
            os.dup2(devnull, 1)
            os.dup2(devnull, 2)
            os.execv(mako, argv)
        _, status, usage = os.wait4(pid, 0)
        if status != 0:
            raise SystemExit(f"native bench: {backend} compile failed")
        compile_stats[backend].append(time.perf_counter_ns() - start)
        rss_stats[backend].append(usage.ru_maxrss)

compile_medians = {name: statistics.median(values)
                   for name, values in compile_stats.items()}
rss_medians = {name: statistics.median(values) for name, values in rss_stats.items()}
compile_ratio = compile_medians["native"] / compile_medians["c"]
rss_ratio = rss_medians["native"] / rss_medians["c"]
print(f"native compile latency: native={compile_medians['native'] / 1_000_000:.3f}ms "
      f"c={compile_medians['c'] / 1_000_000:.3f}ms ratio={compile_ratio:.3f}x "
      f"(max {compile_limit:.3f}x)")
print(f"native compiler max RSS: native={rss_medians['native']:.0f} "
      f"c={rss_medians['c']:.0f} ratio={rss_ratio:.3f}x (max {rss_limit:.3f}x)")
failed |= compile_ratio > compile_limit or rss_ratio > rss_limit

if failed:
    print("native bench: FAILED — a runtime or compiler efficiency bar was exceeded")
    sys.exit(1)
print("native bench: PASS")
PY
bench_status=$?
set -e

echo "native bench artifacts:"
for workload in "${workloads[@]}"; do
  for candidate in native mako-c hand-c rust; do
    binary_bytes="$(wc -c <"$out_dir/$workload-$candidate" | tr -d ' ')"
    echo "  $workload-$candidate binary=$binary_bytes bytes"
  done
done
exit "$bench_status"
