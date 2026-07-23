#!/usr/bin/env bash
# Adversarial + regression gate for whole-file I/O (read/write/append).
# Usage: ./scripts/file-io-regression.sh
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
mako_bin="${MAKO_BIN:-$repo_dir/target/release/mako}"
export MAKO_RUNTIME="${MAKO_RUNTIME:-$repo_dir/runtime}"

if [[ ! -x "$mako_bin" ]]; then
  echo "file-io-regression: building release mako" >&2
  cargo build --release --manifest-path "$repo_dir/Cargo.toml"
fi

# Prefer llvm-backend when the static toolchain is present (macOS product path).
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
  export MAKO_LLD_INCLUDE_DIR="${MAKO_LLD_INCLUDE_DIR:-$toolchain_dir/llvm-project-llvmorg-21.1.8/lld/include}"
  if [[ -n "${LLVM_SYS_211_PREFIX:-}" && -f "${MAKO_LLD_STATIC_DIR}/liblldMachO.a" ]]; then
    if ! "$mako_bin" build --help 2>/dev/null | grep -q 'llvm'; then
      echo "file-io-regression: rebuilding with llvm-backend" >&2
      cargo build --release --features llvm-backend --manifest-path "$repo_dir/Cargo.toml"
    fi
  fi
fi

# Full suite on C + Cranelift native (pointer ABI).
fixtures_full=(
  examples/testing/file_io_adversarial_test.mko
  examples/testing/file_env_test.mko
  examples/testing/fs_polish_test.mko
  examples/testing/fs_storage_test.mko
  examples/testing/storage_portable_io_test.mko
)

# LLVM uses value-ABI strings: only fixtures fully remapped for that ABI.
# Broader fs (dio/mmap/wal) still need per-helper remaps — tracked separately.
fixtures_llvm=(
  examples/testing/file_io_adversarial_test.mko
  examples/testing/file_env_test.mko
  examples/testing/fs_polish_test.mko
)

run_backend() {
  local backend="$1"
  shift
  local fixtures=("$@")
  echo "=== file-io-regression: backend=$backend ==="
  for f in "${fixtures[@]}"; do
    if [[ ! -f "$repo_dir/$f" ]]; then
      echo "file-io-regression: missing $f" >&2
      exit 2
    fi
    echo "  test $f"
    "$mako_bin" test "$repo_dir/$f" --backend "$backend"
  done
  # Bench microkernel must stay output-stable and buildable.
  local out="$repo_dir/out/file-io-regression"
  mkdir -p "$out"
  echo "  build+run native_io bench ($backend)"
  "$mako_bin" build "$repo_dir/examples/bench/native_io.mko" --release \
    --backend "$backend" --no-incremental -o "$out/native_io-$backend"
  local got
  got="$("$out/native_io-$backend")"
  if [[ "$got" != "2048000" ]]; then
    echo "file-io-regression: native_io output want 2048000 got $got (backend=$backend)" >&2
    exit 1
  fi
}

run_backend c "${fixtures_full[@]}"

if [[ "$(uname -s)" != "Windows_NT" && "$(uname -s)" != MINGW* ]]; then
  if "$mako_bin" build --help 2>/dev/null | grep -q 'native'; then
    run_backend native "${fixtures_full[@]}"
  fi
  if "$mako_bin" build --help 2>/dev/null | grep -q 'llvm'; then
    # LLVM may be listed but only work when linked; probe once.
    if "$mako_bin" build "$repo_dir/examples/bench/native_io.mko" --release \
        --backend llvm --no-incremental -o "$repo_dir/out/file-io-regression/_llvm_probe" 2>/dev/null; then
      rm -f "$repo_dir/out/file-io-regression/_llvm_probe"
      run_backend llvm "${fixtures_llvm[@]}"
    else
      echo "file-io-regression: llvm listed but probe failed — skip llvm suite"
    fi
  fi
fi

echo "=== file-io-regression: PASS ==="
