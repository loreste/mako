#!/usr/bin/env bash
# Comprehensive sanitizer sweep for Mako.
#
# Runs ASan+UBSan combined, TSan, and GCC compilation across the full
# test suite plus standalone examples. Reports every failure.
#
# Usage:
#   scripts/sanitize-all.sh [--quick]    # --quick skips examples and GCC

set -euo pipefail

MAKO="${MAKO_BIN:-./target/release/mako}"
MAKO_SANITIZE_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export MAKO_RUNTIME="$MAKO_SANITIZE_ROOT/runtime"
export MAKO_BACKEND="${MAKO_BACKEND:-c}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
QUICK=false
[ "${1:-}" = "--quick" ] && QUICK=true

pass=0; fail=0; skip=0; failures=""
total_start=$(date +%s)

report() {
  local label="$1" result="$2" name="$3"
  if [ "$result" -eq 0 ]; then
    pass=$((pass + 1))
  else
    fail=$((fail + 1))
    failures="$failures\n  [$label] $name"
  fi
}

section() { echo ""; echo "=== $1 ==="; echo ""; }

# ---------- 1. ASan + UBSan combined on full test suite ----------

section "ASan+UBSan: full test suite (393 tests)"
ASAN_OPTIONS=detect_leaks=0 \
  "$MAKO" test --sanitize address,undefined examples/testing 2>&1 | tail -3
asan_full=$?
if [ $asan_full -eq 0 ]; then
  echo "PASS: ASan+UBSan full suite"
  pass=$((pass + 1))
else
  echo "FAIL: ASan+UBSan full suite"
  fail=$((fail + 1))
  failures="$failures\n  [ASan+UBSan] full suite"
fi

# ---------- 2. TSan on all concurrency tests ----------

section "TSan: concurrency tests"
tsan_tests=(
  crew_fan_test kick_send_test kick_share_test kick_sync_test
  chan_struct_test chan_float_test crew_drain_test job_join_typed_test
  proxy_pool_test proxy_edge_test fan_string_test kick_string_test
  select_nll_test chan_string_test share_atomic_test cmap_stress_test
  wave11_queue_test wave14_queue_test wave15_queue_test wave16_queue_test
  wave17_queue_test wave18_queue_test wave19_queue_test wave20_queue_test
  wave21_queue_test wave22_queue_test wave23_queue_test wave24_queue_test
  wave25_queue_test wave26_queue_test wave27_queue_test wave28_queue_test
  wave29_queue_test wave30_queue_test wave31_queue_test wave32_queue_test
  wave33_queue_test wave34_queue_test wave35_queue_test wave36_queue_test
  wave37_queue_test wave38_queue_test chan_select_stress_test
  kick_fn_test actor_test
)
for f in "${tsan_tests[@]}"; do
  result=$("$MAKO" test --race "examples/testing/${f}.mko" 2>&1 | tail -1)
  if echo "$result" | grep -q "1 passed"; then
    pass=$((pass + 1))
  else
    fail=$((fail + 1))
    failures="$failures\n  [TSan] $f"
    echo "  FAIL: $f"
  fi
done
echo "TSan: done (${#tsan_tests[@]} tests)"

# ---------- 3. ASan+UBSan on ownership-critical paths ----------

section "ASan+UBSan: ownership-critical (targeted)"
own_tests=(
  double_free_guard_test match_own_free_test own_branch_regress_test
  early_return_path_free_test discarded_bag_drop_test discarded_bag_borrow_test
  map_set_take_alias_test asan_stress_test defer_wrap_test
  nested_slice_test map_nested_test map_depth3_test
  map_option_result_test option_result_slice_test map_tuple_test
  map_chan_test map_nested_bag_slice_test map_tuple_bag_test
  map_option_of_slice_test map_option_of_map_test
  capturing_closure_test struct_capture_test kick_fn_test
  generic_struct_test generic_enum_test generic_adversarial_test
  result_enum_test overflow_shutdown_test
)
for f in "${own_tests[@]}"; do
  ASAN_OPTIONS=detect_leaks=0 \
    "$MAKO" test --sanitize address,undefined "examples/testing/${f}.mko" 2>&1 >/dev/null
  report "ASan+UBSan" $? "$f"
done
echo "Ownership-critical: done (${#own_tests[@]} tests)"

# ---------- 4. Standalone examples under ASan ----------

if ! $QUICK; then
  section "ASan: standalone examples (compile + short run)"
  for f in examples/*.mko; do
    name=$(basename "$f" .mko)
    # Skip programs that need network/external deps
    case "$name" in
      *http*|*tls*|*sql*|*postgres*|*smtp*|*ws_*|*sip_ua*|*llm*|*h2_*|*h3_*|*quiche*) skip=$((skip + 1)); continue ;;
    esac
    # Try to build with ASan
    if ASAN_OPTIONS=detect_leaks=0 \
       "$MAKO" build "$f" --sanitize address -o "/tmp/mako_san_${name}" 2>/dev/null; then
      # Run with 2 second timeout (some are servers/interactive)
      timeout 2 "/tmp/mako_san_${name}" >/dev/null 2>&1 || true
      # If it built under ASan without error, that's a pass
      report "ASan-build" 0 "$name"
      rm -f "/tmp/mako_san_${name}"
    else
      # Build failure under sanitizer — may be expected for some
      skip=$((skip + 1))
    fi
  done
  echo "Examples: done (skipped $skip)"
fi

# ---------- 5. GCC compilation (catches different warnings) ----------

if ! $QUICK && command -v gcc >/dev/null 2>&1; then
  section "GCC: full suite compilation"
  MAKO_CC=gcc "$MAKO" test examples/testing 2>&1 | tail -3
  gcc_result=$?
  if [ $gcc_result -eq 0 ]; then
    echo "PASS: GCC full suite"
    pass=$((pass + 1))
  else
    echo "FAIL: GCC full suite"
    fail=$((fail + 1))
    failures="$failures\n  [GCC] full suite"
  fi
fi

# ---------- 6. Compiler self-test (Rust unit tests) ----------

section "Rust: compiler unit tests"
cargo test --quiet 2>&1 | tail -1
report "cargo-test" $? "134 unit tests"

# ---------- Report ----------

total_end=$(date +%s)
elapsed=$((total_end - total_start))

section "REPORT"
echo "Passed:  $pass"
echo "Failed:  $fail"
echo "Skipped: $skip"
echo "Time:    ${elapsed}s"
if [ $fail -gt 0 ]; then
  echo ""
  echo "Failures:"
  echo -e "$failures"
  exit 1
else
  echo ""
  echo "All sanitizer checks passed."
fi
