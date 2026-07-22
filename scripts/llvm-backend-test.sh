#!/usr/bin/env bash
# Correctness gate for the statically linked optimizing backend.
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
export MAKO_RUNTIME="$repo_dir/runtime"
toolchain_dir="${MAKO_NATIVE_TOOLCHAIN_DIR:-$repo_dir/out/native-toolchain}"
llvm_tag="llvmorg-21.1.8"

find_llvm_prefix() {
  if [[ -n "${LLVM_SYS_211_PREFIX:-}" ]]; then
    printf '%s\n' "$LLVM_SYS_211_PREFIX"
    return
  fi
  for candidate in /opt/homebrew/opt/llvm@21 /usr/local/opt/llvm@21 /usr/lib/llvm-21; do
    if [[ -x "$candidate/bin/llvm-config" ]]; then
      printf '%s\n' "$candidate"
      return
    fi
  done
  return 1
}

export LLVM_SYS_211_PREFIX="$(find_llvm_prefix || true)"
export MAKO_LLD_STATIC_DIR="${MAKO_LLD_STATIC_DIR:-$toolchain_dir/lld-build/lib}"
export MAKO_LLD_INCLUDE_DIR="${MAKO_LLD_INCLUDE_DIR:-$toolchain_dir/llvm-project-$llvm_tag/lld/include}"
if [[ -z "$LLVM_SYS_211_PREFIX" || ! -f "$MAKO_LLD_STATIC_DIR/liblldMachO.a" || \
      ! -f "$MAKO_LLD_INCLUDE_DIR/lld/Common/Driver.h" ]]; then
  echo "LLVM backend test: run scripts/bootstrap-native-toolchain.sh first" >&2
  exit 2
fi

tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/mako-llvm-test.XXXXXX")"
trap 'rm -rf "$tmp_dir"' EXIT
scalar_fixture="$repo_dir/examples/native/native_fibonacci.mko"
string_fixture="$repo_dir/examples/native/native_strings.mko"
slice_fixture="$repo_dir/examples/native/native_slices.mko"

echo "[1/4] shared IR and LLVM emitter unit tests"
cargo test --manifest-path "$repo_dir/Cargo.toml" --features llvm-backend --bin mako native_ir
cargo test --manifest-path "$repo_dir/Cargo.toml" --features llvm-backend --bin mako llvm_codegen

echo "[2/4] C/LLVM differential execution"
cargo build --manifest-path "$repo_dir/Cargo.toml" --features llvm-backend
for fixture in "$scalar_fixture" "$string_fixture" "$slice_fixture"; do
  name="$(basename "$fixture" .mko)"
  "$repo_dir/target/debug/mako" build "$fixture" --release --backend c \
    --no-incremental -o "$tmp_dir/$name-c"
  env -u SDKROOT PATH=/nonexistent "$repo_dir/target/debug/mako" build "$fixture" \
    --release --backend llvm -o "$tmp_dir/$name-llvm"
  "$tmp_dir/$name-c" >"$tmp_dir/$name.c.out"
  "$tmp_dir/$name-llvm" >"$tmp_dir/$name.llvm.out"
  cmp "$tmp_dir/$name.c.out" "$tmp_dir/$name.llvm.out"
done

echo "[3/4] LLVM heap ownership memory safety"
if [[ "$(uname -s)" == "Darwin" ]]; then
  if [[ -r /usr/lib/libgmalloc.dylib ]]; then
    DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib \
      MallocScribble=1 MallocPreScribble=1 "$tmp_dir/native_strings-llvm" >/dev/null
  fi
  set +e
  leaks --atExit -- "$tmp_dir/native_strings-llvm" >"$tmp_dir/strings.leaks" 2>&1
  leaks_status=$?
  set -e
  if grep -Eq "([1-9][0-9]* leaks for|[1-9][0-9]* total leaked bytes)" \
      "$tmp_dir/strings.leaks"; then
    grep -E "leaks for|leaked bytes" "$tmp_dir/strings.leaks" >&2
    exit 1
  fi
  if [[ $leaks_status -ne 0 ]] && ! grep -q "Couldn't get task port" "$tmp_dir/strings.leaks"; then
    tail -20 "$tmp_dir/strings.leaks" >&2
    exit 1
  fi
  set +e
  leaks --atExit -- "$tmp_dir/native_slices-llvm" >"$tmp_dir/slices.leaks" 2>&1
  leaks_status=$?
  set -e
  if grep -Eq "([1-9][0-9]* leaks for|[1-9][0-9]* total leaked bytes)" "$tmp_dir/slices.leaks"; then
    grep -E "leaks for|leaked bytes" "$tmp_dir/slices.leaks" >&2
    exit 1
  fi
  if [[ $leaks_status -ne 0 ]] && ! grep -q "Couldn't get task port" "$tmp_dir/slices.leaks"; then
    tail -20 "$tmp_dir/slices.leaks" >&2
    exit 1
  fi
fi

echo "[4/5] aggregate + []string differential (no C fallback)"
"$tmp_dir/native_slices-llvm" >/dev/null
for fixture in \
  "$repo_dir/examples/native/native_structs.mko" \
  "$repo_dir/examples/native/native_string_slices.mko" \
  "$repo_dir/examples/native/native_nested_structs.mko" \
  "$repo_dir/examples/native/native_match_owned.mko" \
  "$repo_dir/examples/native/native_for.mko" \
  "$repo_dir/examples/native/native_match_guards.mko" \
  "$repo_dir/examples/native/native_defer.mko" \
  "$repo_dir/examples/native/native_cfor.mko" \
  "$repo_dir/examples/native/native_labeled.mko" \
  "$repo_dir/examples/native/native_fmt.mko" \
  "$repo_dir/examples/native/native_match.mko" \
  "$repo_dir/examples/native/native_mem_stress.mko" \
  "$repo_dir/examples/native/native_if_expr.mko" \
  "$repo_dir/examples/native/native_methods.mko" \
  "$repo_dir/examples/native/native_builtins.mko" \
  "$repo_dir/examples/native/native_maps.mko" \
  "$repo_dir/examples/native/native_result.mko"
do
  name="$(basename "$fixture" .mko)"
  env -u SDKROOT PATH=/nonexistent "$repo_dir/target/debug/mako" build "$fixture" \
    --release --backend llvm -o "$tmp_dir/$name-llvm"
  "$repo_dir/target/debug/mako" build "$fixture" --release --backend c \
    --no-incremental -o "$tmp_dir/$name-c"
  "$tmp_dir/$name-c" >"$tmp_dir/$name.c.out"
  "$tmp_dir/$name-llvm" >"$tmp_dir/$name.llvm.out"
  cmp "$tmp_dir/$name.c.out" "$tmp_dir/$name.llvm.out"
  # Memory safety is mandatory on every heap fixture.
  if [[ "$(uname -s)" == "Darwin" && -r /usr/lib/libgmalloc.dylib ]]; then
    DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib \
      MallocScribble=1 MallocPreScribble=1 "$tmp_dir/$name-llvm" >/dev/null
  fi
  if [[ "$(uname -s)" == "Darwin" ]]; then
    set +e
    leaks --atExit -- "$tmp_dir/$name-llvm" >"$tmp_dir/$name-llvm.leaks" 2>&1
    set -e
    if grep -Eq "([1-9][0-9]* leaks for|[1-9][0-9]* total leaked bytes)" \
      "$tmp_dir/$name-llvm.leaks"; then
      echo "LLVM backend test: leaks in $name" >&2
      grep -E "leaks for|leaked bytes" "$tmp_dir/$name-llvm.leaks" >&2 || true
      exit 1
    fi
  fi
done
# Generics remain unsupported and must hard-error (no silent C fallback).
if env PATH=/nonexistent "$repo_dir/target/debug/mako" build \
  "$repo_dir/examples/generics_user.mko" --release --backend llvm \
  -o "$tmp_dir/unsupported" >"$tmp_dir/unsupported.out" 2>&1; then
  echo "LLVM backend test: generics unexpectedly compiled" >&2
  exit 1
fi
grep -Eq "unsupported|not implemented|not in the scalar|generic|failed" "$tmp_dir/unsupported.out" || {
  echo "LLVM backend test: unsupported construct did not produce a diagnostic" >&2
  cat "$tmp_dir/unsupported.out" >&2
  exit 1
}

echo "[5/5] LLVM release performance and artifact parity"
# Keep these gates opt-out configurable for slower CI hosts, but make the
# comparison part of the authoritative LLVM smoke test.  A single warmup and
# three alternating samples are enough to catch accidental debug/fallback
# regressions without turning this script into a benchmark suite.
samples="${MAKO_LLVM_GATE_SAMPLES:-3}"
python3 - "$samples" "$repo_dir/target/debug/mako" "$scalar_fixture" "$tmp_dir" <<'PY'
import os, statistics, subprocess, sys, time
samples = int(sys.argv[1]); mako = sys.argv[2]; fixture = sys.argv[3]; out = sys.argv[4]
paths = {}
compile_stats = {}
for backend in ("c", "llvm"):
    path = os.path.join(out, "gate-" + backend)
    vals = []
    for i in range(samples):
        start = time.perf_counter_ns()
        env = os.environ.copy()
        if backend == "llvm":
            env.pop("SDKROOT", None); env["PATH"] = "/nonexistent"
        subprocess.run([mako, "build", fixture, "--release", "--backend", backend,
                        "--no-incremental", "-o", path], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
        vals.append(time.perf_counter_ns() - start)
    compile_stats[backend] = vals
    paths[backend] = path
def run(path):
    t = time.perf_counter_ns(); pid = os.fork()
    if pid == 0:
        devnull = os.open(os.devnull, os.O_WRONLY); os.dup2(devnull, 1)
        os.execv(path, [path])
    _, status, usage = os.wait4(pid, 0)
    if status != 0: raise SystemExit("LLVM gate: generated program failed")
    return time.perf_counter_ns()-t, usage.ru_maxrss
for p in paths.values(): run(p)  # warmup
stats = {k: [run(v) for _ in range(samples)] for k,v in paths.items()}
for k, vals in stats.items():
    print("LLVM gate", k, "runtime-ms=%.3f rss=%d" %
          (statistics.median(x[0] for x in vals)/1e6,
           statistics.median(x[1] for x in vals)))
ratio = statistics.median(x[0] for x in stats["llvm"]) / statistics.median(x[0] for x in stats["c"])
rss_ratio = statistics.median(x[1] for x in stats["llvm"]) / statistics.median(x[1] for x in stats["c"])
size_ratio = os.path.getsize(paths["llvm"]) / os.path.getsize(paths["c"])
compile_ratio = statistics.median(compile_stats["llvm"]) / statistics.median(compile_stats["c"])
print("LLVM gate ratios: compile=%.3fx runtime=%.3fx rss=%.3fx binary=%.3fx" %
      (compile_ratio, ratio, rss_ratio, size_ratio))
if compile_ratio > float(os.environ.get("MAKO_LLVM_COMPILE_RATIO", "2.00")):
    raise SystemExit("LLVM gate: compile-latency parity bar exceeded")
if ratio > float(os.environ.get("MAKO_LLVM_RUNTIME_RATIO", "1.10")):
    raise SystemExit("LLVM gate: runtime parity bar exceeded")
if rss_ratio > float(os.environ.get("MAKO_LLVM_RUNTIME_RSS_RATIO", "1.25")):
    raise SystemExit("LLVM gate: RSS parity bar exceeded")
if size_ratio > float(os.environ.get("MAKO_LLVM_BINARY_RATIO", "1.25")):
    raise SystemExit("LLVM gate: binary-size parity bar exceeded")
PY

echo "LLVM backend test: all checks passed"
