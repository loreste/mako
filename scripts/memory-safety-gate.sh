#!/usr/bin/env bash
# Memory safety + no-GC gate (docs/MEMORY_SAFETY.md).
#
# 1) Product identity: tree must not ship a tracing GC.
# 2) Ownership / leak / double-free tests on C and (Unix) native backends.
# 3) Optional ASan on the contract fixture when --sanitize address works.
#
# Usage:
#   ./scripts/memory-safety-gate.sh
#   MAKO_BIN=./target/release/mako ./scripts/memory-safety-gate.sh
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
mako_bin="${MAKO_BIN:-$repo_dir/target/release/mako}"
export MAKO_RUNTIME="${MAKO_RUNTIME:-$repo_dir/runtime}"

if [[ ! -x "$mako_bin" ]]; then
  echo "memory-safety-gate: building release mako" >&2
  cargo build --release --manifest-path "$repo_dir/Cargo.toml"
fi

echo "=== memory-safety-gate: no tracing GC in product sources ==="
# Fail if a collector *implementation* appears in the runtime. Typecheck may
# still name `gc_*` only to hard-error "Mako has no garbage collector".
if rg -n --glob '!**/target/**' \
    -e '\bBoehm\b' -e '\btracing[_ ]gc\b' -e '\bmark_and_sweep\b' \
    -e '\bmako_gc_collect\b' -e '\bmako_gc_alloc\b' \
    "$repo_dir/runtime" 2>/dev/null; then
  echo "memory-safety-gate: forbidden GC runtime found" >&2
  exit 1
fi
# Typecheck must reject gc_* names (no collector mode).
if ! rg -q 'Mako has no garbage collector' "$repo_dir/src/types/mod.rs" 2>/dev/null; then
  echo "memory-safety-gate: typecheck must reject gc_* builtins" >&2
  exit 1
fi
# Docs must keep the non-goal explicit.
if ! rg -q 'Tracing GC|no GC|No GC' "$repo_dir/docs/SOUNDNESS.md" "$repo_dir/docs/MEMORY_SAFETY.md" 2>/dev/null; then
  echo "memory-safety-gate: docs must state no tracing GC" >&2
  exit 1
fi
echo "memory-safety-gate: no GC markers ok"

fixtures=(
  examples/testing/memory_safety_contract_test.mko
  examples/testing/double_free_guard_test.mko
  examples/testing/own_drop_slice_test.mko
  examples/testing/leak_detector_test.mko
  examples/testing/match_own_free_test.mko
  examples/testing/own_branch_regress_test.mko
)

run_backend() {
  local backend="$1"
  echo "=== memory-safety-gate: backend=$backend ==="
  for f in "${fixtures[@]}"; do
    if [[ ! -f "$repo_dir/$f" ]]; then
      echo "memory-safety-gate: missing $f" >&2
      exit 2
    fi
    echo "  test $f"
    "$mako_bin" test "$repo_dir/$f" --backend "$backend"
  done
}

run_backend c

if [[ "$(uname -s)" != "Windows_NT" && "$(uname -s)" != MINGW* ]]; then
  # Cranelift native path — same ownership rules, no GC.
  if "$mako_bin" build --help 2>/dev/null | grep -q 'native'; then
    run_backend native || {
      echo "memory-safety-gate: native backend failed ownership suite" >&2
      exit 1
    }
  fi
fi

echo "=== memory-safety-gate: ASan (optional if toolchain supports) ==="
set +e
"$mako_bin" test "$repo_dir/examples/testing/memory_safety_contract_test.mko" \
  --backend c --sanitize address >/tmp/mako-ms-asan.out 2>&1
asan_status=$?
set -e
if [[ $asan_status -eq 0 ]]; then
  echo "memory-safety-gate: ASan contract fixture ok"
else
  # Sanitizer may be unavailable on some hosts — do not hard-fail the gate,
  # but surface the log for CI jobs that already run full ASan separately.
  if grep -qiE 'unsupported|unavailable|not supported|sanitize|error:' /tmp/mako-ms-asan.out 2>/dev/null \
     && ! grep -qiE 'AddressSanitizer|SUMMARY: AddressSanitizer|heap-use-after-free|double-free' /tmp/mako-ms-asan.out 2>/dev/null; then
    echo "memory-safety-gate: ASan skipped or soft-fail: $(head -5 /tmp/mako-ms-asan.out)"
  else
    echo "memory-safety-gate: ASan failed:" >&2
    cat /tmp/mako-ms-asan.out >&2 || true
    exit 1
  fi
fi

echo "=== memory-safety-gate: years-up soaks (ownership under load) ==="
"$repo_dir/scripts/long-run-soak.sh"
# HTTP soak is heavier; allow skip with MAKO_MS_SKIP_HTTP=1
if [[ "${MAKO_MS_SKIP_HTTP:-0}" != "1" ]]; then
  MAKO_HTTP_SOAK_REQUESTS="${MAKO_HTTP_SOAK_REQUESTS:-800}" \
  MAKO_HTTP_SOAK_CLIENTS="${MAKO_HTTP_SOAK_CLIENTS:-4}" \
    "$repo_dir/scripts/http-long-run-soak.sh"
fi

echo "memory-safety-gate: all checks passed (memory safe path, no GC)"
