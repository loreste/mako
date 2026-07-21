#!/usr/bin/env bash
# Prepare the static LLVM/lld build inputs used to produce the single-file mako
# compiler. Generated sources and archives stay under ignored out/.
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
toolchain_dir="${MAKO_NATIVE_TOOLCHAIN_DIR:-$repo_dir/out/native-toolchain}"
llvm_tag="llvmorg-21.1.8"
llvm_source="$toolchain_dir/llvm-project-$llvm_tag"
lld_build="$toolchain_dir/lld-build"

find_llvm_prefix() {
  if [[ -n "${MAKO_LLVM_PREFIX:-}" ]]; then
    printf '%s\n' "$MAKO_LLVM_PREFIX"
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

llvm_prefix="$(find_llvm_prefix || true)"
if [[ -z "$llvm_prefix" ]]; then
  echo "native toolchain: LLVM 21 development files are required" >&2
  echo "set MAKO_LLVM_PREFIX to a prefix containing bin/llvm-config" >&2
  exit 2
fi
llvm_version="$($llvm_prefix/bin/llvm-config --version)"
if [[ "$llvm_version" != 21.* ]]; then
  echo "native toolchain: expected LLVM 21, found $llvm_version" >&2
  exit 2
fi
if ! command -v cmake >/dev/null 2>&1; then
  echo "native toolchain: cmake is required to build static lld" >&2
  exit 2
fi
if ! command -v git >/dev/null 2>&1; then
  echo "native toolchain: git is required to fetch pinned lld sources" >&2
  exit 2
fi

mkdir -p "$toolchain_dir"
if [[ ! -d "$llvm_source/.git" ]]; then
  git clone --depth 1 --filter=blob:none --sparse --branch "$llvm_tag" \
    https://github.com/llvm/llvm-project.git "$llvm_source"
fi
git -C "$llvm_source" sparse-checkout set lld cmake llvm/cmake llvm/include

cmake -S "$llvm_source/lld" -B "$lld_build" \
  -DLLVM_DIR="$llvm_prefix/lib/cmake/llvm" \
  -DLLVM_INCLUDE_TESTS=OFF \
  -DBUILD_SHARED_LIBS=OFF \
  -DLLD_BUILD_TOOLS=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$lld_build" --target lldCommon lldMachO --parallel

for archive in liblldCommon.a liblldMachO.a; do
  if [[ ! -f "$lld_build/lib/$archive" ]]; then
    echo "native toolchain: missing static archive $archive" >&2
    exit 1
  fi
done

echo "native toolchain: LLVM $llvm_version at $llvm_prefix"
echo "native toolchain: static lld archives at $lld_build/lib"
echo "export LLVM_SYS_211_PREFIX=$llvm_prefix"
echo "export MAKO_LLD_STATIC_DIR=$lld_build/lib"
echo "export MAKO_LLD_INCLUDE_DIR=$llvm_source/lld/include"
