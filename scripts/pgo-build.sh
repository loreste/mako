#!/usr/bin/env bash
# Profile-guided optimization recipe for release servers (docs/LONG_RUNNING.md · LR-4).
#
# Two-pass clang PGO:
#   1) Instrument with MAKO_PGO_GEN
#   2) Train under representative load
#   3) Merge profiles (clang) and rebuild with MAKO_PGO_USE
#
# Usage:
#   ./scripts/pgo-build.sh examples/bench/http_long_run_server.mko -o out/http_pgo
#   ./scripts/pgo-build.sh examples/bench/http_long_run_server.mko -o out/http_pgo -- \
#       500 19812
#
# Env:
#   MAKO_BIN          — mako compiler (default target/release/mako)
#   MAKO_PGO_PROFDIR  — where to write .profraw / .profdata (default out/pgo-prof)
#   MAKO_PGO_BACKEND  — c (default; PGO is a C/clang path today)
#   MAKO_ALLOCATOR    — optional mimalloc|jemalloc for both passes
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
mako_bin="${MAKO_BIN:-$repo_dir/target/release/mako}"
profdir="${MAKO_PGO_PROFDIR:-$repo_dir/out/pgo-prof}"
backend="${MAKO_PGO_BACKEND:-c}"

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <source.mko> -o <outbin> [-- train args...]" >&2
  exit 2
fi

src=""
out=""
train_args=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    -o)
      out="${2:-}"
      shift 2
      ;;
    --)
      shift
      train_args=("$@")
      break
      ;;
    *)
      if [[ -z "$src" ]]; then
        src="$1"
      else
        echo "unexpected arg: $1" >&2
        exit 2
      fi
      shift
      ;;
  esac
done

if [[ -z "$src" || -z "$out" ]]; then
  echo "usage: $0 <source.mko> -o <outbin> [-- train args...]" >&2
  exit 2
fi
if [[ ! -x "$mako_bin" ]]; then
  cargo build --release --manifest-path "$repo_dir/Cargo.toml"
fi

mkdir -p "$profdir" "$(dirname "$out")"
instr="$out.pgo-gen"
profraw="$profdir/default-%p.profraw"
profdata="$profdir/merged.profdata"

echo "pgo-build: [1/3] instrumented build → $instr"
rm -f "$instr" "$profdir"/*.profraw "$profdata" 2>/dev/null || true
# LLVM_PROFILE_FILE is used by clang instrumentation at train time.
export LLVM_PROFILE_FILE="$profraw"
MAKO_PGO_GEN=1 "$mako_bin" build "$src" --release --backend "$backend" --no-incremental -o "$instr"

echo "pgo-build: [2/3] train"
# Default train: run once with any provided args; for HTTP soak server, pass request count.
if [[ ${#train_args[@]} -gt 0 ]]; then
  "$instr" "${train_args[@]}" || true
else
  # Fallback train: run the binary with no args (may be a short CLI).
  "$instr" || true
fi

find_llvm_profdata() {
  if command -v llvm-profdata >/dev/null 2>&1; then
    command -v llvm-profdata
    return
  fi
  for c in \
    "${LLVM_SYS_211_PREFIX:-}/bin/llvm-profdata" \
    /opt/homebrew/opt/llvm@21/bin/llvm-profdata \
    /opt/homebrew/opt/llvm/bin/llvm-profdata \
    /usr/local/opt/llvm@21/bin/llvm-profdata \
    /usr/bin/llvm-profdata
  do
    if [[ -x "$c" ]]; then
      printf '%s\n' "$c"
      return
    fi
  done
  return 1
}

# Prefer llvm-profdata when present (clang / Apple clang instrumentation).
merge_ok=0
shopt -s nullglob
raws=("$profdir"/*.profraw)
shopt -u nullglob
if [[ ${#raws[@]} -gt 0 ]]; then
  if profdata_bin="$(find_llvm_profdata)"; then
    echo "pgo-build: merging ${#raws[@]} profraw with $profdata_bin"
    "$profdata_bin" merge -o "$profdata" "${raws[@]}"
    merge_ok=1
  else
    echo "pgo-build: warning: found .profraw but no llvm-profdata (install llvm@21)" >&2
  fi
fi

echo "pgo-build: [3/3] optimized rebuild → $out"
if [[ $merge_ok -eq 1 ]]; then
  MAKO_PGO_USE="$profdata" "$mako_bin" build "$src" --release --backend "$backend" \
    --no-incremental -o "$out"
else
  # Do not pass a missing default.profdata path — rebuild without PGO use.
  echo "pgo-build: warning: no merged profile — release rebuild without MAKO_PGO_USE" >&2
  "$mako_bin" build "$src" --release --backend "$backend" --no-incremental -o "$out"
fi

echo "pgo-build: done $out"
ls -la "$out"
