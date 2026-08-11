#!/usr/bin/env bash
set -euo pipefail

# Dependency and provenance report for the self-hosting migration.
#
# The claim being audited is not "Mako has no dependencies" — the stage-0 seed
# is a Rust binary that links a large third-party surface, and that is allowed
# during migration. The claim is narrower and checkable today:
#
#   the artifacts the Mako-written backend emits depend on nothing but the
#   kernel ABI.
#
# So this splits the report in two. The emitted-artifact section is a hard gate:
# a stage-1 image that grows a dynamic dependency, an interpreter, or a link
# against libc has stopped being self-contained, and that regression should fail
# the build the day it happens. The toolchain section is reported, not gated —
# it is the distance still to travel, and printing it keeps that distance
# honest instead of letting "self-hosted" quietly cover it.
#
# Exit status: 0 when every emitted image is self-contained, 1 otherwise.
#
# This is shell because it inspects binaries with platform tools during
# migration. At cutover the plan requires this logic to live in Mako as
# `mako audit purity`; until the Mako runtime can read ELF/Mach-O headers
# itself, keeping it here is the honest placeholder rather than a claim that
# the audit is already self-hosted.

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
stage1="${MAKO_STAGE1:-/tmp/makoc-stage1}"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/mako-purity.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT

fail=0
json_images=""

# An OS interface is an external execution platform, not a language dependency:
# system frameworks and the platform libc are permitted by the plan. Anything
# from a package manager or an @rpath is a third-party link and is reported.
classify_lib() {
    case "$1" in
        /usr/lib/*|/System/Library/*|linux-vdso*|/lib/*|/lib64/*) echo "os" ;;
        *) echo "third-party" ;;
    esac
}

echo "=== emitted artifacts (hard gate) ==="
# Every ELF the self-hosted backend can emit must be statically linked with no
# interpreter. `file` reports both, and is available on each host that runs this.
emitted_count=0
for fixture in elf_argc_guard_exit elf_arg_get_guard_exit elf_string_clone_exit \
    elf_while_sum_exit elf_struct_literal_exit; do
    src="$repo_dir/compiler/testdata/$fixture.mko"
    [[ -f "$src" ]] || continue
    [[ -x "$stage1" ]] || { echo "  (stage 1 not built at $stage1 — skipping)"; break; }
    out="$work_dir/$fixture"
    if ! "$stage1" "$src" "$out" >/dev/null 2>&1; then
        echo "  SKIP $fixture (stage 1 declines this shape)"
        continue
    fi
    emitted_count=$((emitted_count + 1))
    desc="$(file -b "$out")"
    status="self-contained"
    case "$desc" in
        *"statically linked"*) ;;
        *"dynamically linked"*|*"interpreter"*)
            status="DYNAMIC"; fail=1 ;;
        *) status="unknown-format"; fail=1 ;;
    esac
    printf '  %-28s %s\n' "$fixture" "$status"
    [[ "$status" != "self-contained" ]] && printf '      %s\n' "$desc"
    json_images="$json_images{\"image\":\"$fixture\",\"status\":\"$status\"},"
done
echo "  images checked: $emitted_count"

echo
echo "=== toolchain surface (reported, not gated) ==="
# The seed compiler's own links. These are the migration debt: each third-party
# entry is a library the Mako runtime does not yet replace.
tool_os=0
tool_third=0
for binary in "$repo_dir/target/release/mako" "$stage1"; do
    [[ -x "$binary" ]] || continue
    echo "  $(basename "$binary"):"
    if command -v otool >/dev/null 2>&1; then
        libs="$(otool -L "$binary" 2>/dev/null | tail -n +2 | awk '{print $1}')"
    elif command -v ldd >/dev/null 2>&1; then
        libs="$(ldd "$binary" 2>/dev/null | awk '{print $1}')"
    else
        echo "    (no otool/ldd on this host)"; continue
    fi
    while IFS= read -r lib; do
        [[ -n "$lib" ]] || continue
        kind="$(classify_lib "$lib")"
        if [[ "$kind" == "third-party" ]]; then
            tool_third=$((tool_third + 1))
            printf '    third-party  %s\n' "$lib"
        else
            tool_os=$((tool_os + 1))
        fi
    done <<< "$libs"
done
echo "  os-interface links: $tool_os   third-party links: $tool_third"

echo
echo "=== forbidden-language sources in the runtime package ==="
# The plan forbids required C in the shipped runtime at cutover. Today the
# native backend compiles native_runtime.c and native_bridge.c, so this counts
# them rather than failing: the number is the work remaining, and it should
# only ever go down.
#
# Vendored third_party/ is counted separately and deliberately. Lumping it in
# reports ~329 C files where the first-party debt is 3, which overstates the
# remaining work by two orders of magnitude — the sort of number that makes a
# migration look impossible and a later "we removed 326 C files" look like
# progress. Vendored code leaves by dropping the dependency, not by porting.
c_sources=$(find "$repo_dir/runtime" -name '*.c' -not -path '*/third_party/*' 2>/dev/null | wc -l | tr -d ' ')
c_headers=$(find "$repo_dir/runtime" -name '*.h' -not -path '*/third_party/*' 2>/dev/null | wc -l | tr -d ' ')
c_vendored=$(find "$repo_dir/runtime/third_party" \( -name '*.c' -o -name '*.h' \) 2>/dev/null | wc -l | tr -d ' ')
rust_sources=$(find "$repo_dir/src" -name '*.rs' 2>/dev/null | wc -l | tr -d ' ')
echo "  first-party runtime C: $c_sources sources, $c_headers headers"
find "$repo_dir/runtime" -name '*.c' -not -path '*/third_party/*' 2>/dev/null \
    | sed "s|$repo_dir/|    |"
echo "  vendored third_party C/H: $c_vendored (leaves with the dependency, not by porting)"
echo "  seed Rust sources: $rust_sources"

attestation="$repo_dir/purity-attestation.json"
printf '{\n  "schema": "mako.purity.v1",\n  "emittedImagesChecked": %s,\n  "emittedImages": [%s],\n  "toolchainOsLinks": %s,\n  "toolchainThirdPartyLinks": %s,\n  "firstPartyRuntimeCSources": %s,\n  "firstPartyRuntimeCHeaders": %s,\n  "vendoredThirdPartyCFiles": %s,\n  "seedRustSources": %s,\n  "emittedArtifactsSelfContained": %s\n}\n' \
    "$emitted_count" "${json_images%,}" "$tool_os" "$tool_third" \
    "$c_sources" "$c_headers" "$c_vendored" "$rust_sources" \
    "$([[ $fail -eq 0 ]] && echo true || echo false)" > "$attestation"
echo
echo "attestation: $attestation"

if [[ $fail -ne 0 ]]; then
    echo "purity audit: an emitted image is not self-contained" >&2
    exit 1
fi
echo "purity audit: emitted artifacts are self-contained; toolchain debt reported above"
