#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: $0 <target-triple> <artifact> [--static]" >&2
    exit 2
}

[[ $# -ge 2 && $# -le 3 ]] || usage
target=$1
artifact=$2
static=0
if [[ ${3:-} == "--static" ]]; then
    static=1
elif [[ $# -eq 3 ]]; then
    usage
fi

if [[ ! -f "$artifact" || ! -s "$artifact" ]]; then
    echo "artifact verification failed: missing or empty artifact: $artifact" >&2
    exit 1
fi
if ! command -v file >/dev/null 2>&1; then
    echo "artifact verification failed: file(1) is required" >&2
    exit 1
fi

description=$(file -b "$artifact")
case "$target" in
    x86_64-unknown-linux-*)
        [[ $description == ELF\ 64-bit* && $description == *x86-64* ]] || {
            echo "artifact verification failed: $target is not x86-64 ELF: $description" >&2
            exit 1
        }
        ;;
    aarch64-unknown-linux-*)
        [[ $description == ELF\ 64-bit* && $description == *ARM\ aarch64* ]] || {
            echo "artifact verification failed: $target is not AArch64 ELF: $description" >&2
            exit 1
        }
        ;;
    x86_64-pc-windows-gnu)
        [[ $description == PE32+* && $description == *x86-64* ]] || {
            echo "artifact verification failed: $target is not x86-64 PE32+: $description" >&2
            exit 1
        }
        ;;
    x86_64-apple-darwin)
        [[ $description == Mach-O\ 64-bit* && $description == *x86_64* ]] || {
            echo "artifact verification failed: $target is not x86-64 Mach-O: $description" >&2
            exit 1
        }
        ;;
    aarch64-apple-darwin)
        [[ $description == Mach-O\ 64-bit* && $description == *arm64* ]] || {
            echo "artifact verification failed: $target is not arm64 Mach-O: $description" >&2
            exit 1
        }
        ;;
    wasm32-wasi|wasm32-wasip1|wasm32-unknown-wasi)
        [[ $description == WebAssembly* ]] || {
            echo "artifact verification failed: $target is not WebAssembly: $description" >&2
            exit 1
        }
        ;;
    *)
        echo "artifact verification failed: no format contract for target $target" >&2
        exit 2
        ;;
esac

if (( static )); then
    [[ $description == *statically\ linked* ]] || {
        echo "artifact verification failed: $target is not reported as statically linked: $description" >&2
        exit 1
    }
    readelf_bin=""
    if command -v readelf >/dev/null 2>&1; then
        readelf_bin=$(command -v readelf)
    elif command -v llvm-readelf >/dev/null 2>&1; then
        readelf_bin=$(command -v llvm-readelf)
    fi
    if [[ -n "$readelf_bin" ]]; then
        if "$readelf_bin" -l "$artifact" 2>/dev/null | grep -Eq '[[:space:]]INTERP[[:space:]]'; then
            echo "artifact verification failed: $artifact contains an ELF interpreter" >&2
            exit 1
        fi
    else
        echo "warning: readelf/llvm-readelf unavailable; relying on file(1)'s static-link classification" >&2
    fi
fi

if (( static )); then
    echo "verified $target: $description (static contract checked)"
else
    echo "verified $target: $description"
fi
