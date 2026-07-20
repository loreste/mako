#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
mako_bin="${MAKO_TEST_COMPILER:-$repo_dir/target/debug/mako}"
export MAKO_RUNTIME="${MAKO_TEST_RUNTIME:-$repo_dir/runtime}"

tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/mako-native-test.XXXXXX")"
cleanup() {
  rm -rf "$tmp_dir"
}
trap cleanup EXIT
selfhost_bin="$tmp_dir/makoc-stage1"

echo "[1/5] Rust compiler unit tests"
cargo test --manifest-path "$repo_dir/Cargo.toml"

if [[ ! -x "$mako_bin" ]]; then
  cargo build --manifest-path "$repo_dir/Cargo.toml"
fi

echo "[2/5] self-hosted frontend gate"
MAKO_STAGE1_OUT="$selfhost_bin" "$repo_dir/scripts/selfhost-gate.sh"

echo "[3/5] ownership regression"
"$mako_bin" test "$repo_dir/examples/testing/append_move_test.mko" --verbose

echo "[4/5] instrumented memory-safety regression"
memory_bin="$tmp_dir/append-move-memory"
if [[ "$(uname -s)" == "Darwin" && -r /usr/lib/libgmalloc.dylib ]]; then
  "$mako_bin" build "$repo_dir/examples/testing/append_move_memory.mko" --no-incremental -o "$memory_bin"
  DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib \
    MallocScribble=1 \
    MallocPreScribble=1 \
    "$memory_bin"
  DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib \
    MallocScribble=1 \
    MallocPreScribble=1 \
    "$selfhost_bin" "$repo_dir/compiler/testdata/literals.mko" >/dev/null
else
  "$mako_bin" build "$repo_dir/examples/testing/append_move_memory.mko" \
    --sanitize address --no-incremental -o "$memory_bin"
  ASAN_OPTIONS="abort_on_error=1:detect_leaks=1" "$memory_bin"
fi

echo "[4b/5] native backend heap ownership memory safety"
for owned_fixture in native_strings native_slices; do
  native_mem="$tmp_dir/$owned_fixture"
  "$mako_bin" build "$repo_dir/examples/native/$owned_fixture.mko" --backend native -o "$native_mem"
  if [[ "$(uname -s)" == "Darwin" ]]; then
    if [[ -r /usr/lib/libgmalloc.dylib ]]; then
      DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib \
        MallocScribble=1 MallocPreScribble=1 "$native_mem" >/dev/null
    fi
    # Fail on any leaked buffer from the ownership/drop pass.
    if ! leaks --atExit -- "$native_mem" >"$tmp_dir/$owned_fixture-leaks.txt" 2>/dev/null; then
      echo "native compiler test: leaks detected in $owned_fixture ownership" >&2
      grep -E "leaks for|leaked bytes" "$tmp_dir/$owned_fixture-leaks.txt" >&2 || true
      exit 1
    fi
  else
    "$native_mem" >/dev/null
  fi
done

echo "[5/5] C/native differential execution"
for fixture in \
  "examples/hello.mko" \
  "examples/integers.mko" \
  "examples/native/native_strings.mko" \
  "examples/native/native_slices.mko" \
  "examples/native/native_structs.mko" \
  "examples/native/native_for.mko" \
  "examples/native/native_match.mko"
do
  name="$(basename "$fixture" .mko)"
  "$mako_bin" run "$repo_dir/$fixture" --backend c --no-incremental >"$tmp_dir/$name.c.out"
  "$mako_bin" run "$repo_dir/$fixture" --backend native >"$tmp_dir/$name.native.out"
  if ! cmp -s "$tmp_dir/$name.c.out" "$tmp_dir/$name.native.out"; then
    echo "native compiler test: backend output mismatch for $fixture" >&2
    diff -u "$tmp_dir/$name.c.out" "$tmp_dir/$name.native.out" >&2 || true
    exit 1
  fi
done

echo "native compiler test: all checks passed"
