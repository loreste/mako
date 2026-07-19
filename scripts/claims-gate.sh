#!/usr/bin/env bash
set -euo pipefail

# Local evidence gate for the product claims described by the public docs.
# Run from any directory after building target/release/mako.
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
MAKO=${MAKO_BIN:-$ROOT/target/release/mako}
# Prefer in-tree std/runtime so gates track the checkout, not a stale install.
export MAKO_STD=${MAKO_STD:-$ROOT/std}
export MAKO_RUNTIME=${MAKO_RUNTIME:-$ROOT/runtime}
CACHE=${MAKO_CACHE:-/tmp/mako-claims-gate-cache-$(date +%s)}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/mako-claims.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

if ! command -v rg >/dev/null 2>&1; then
    echo "claims-gate: ripgrep (rg) is required" >&2
    exit 1
fi

if git -C "$ROOT" ls-files --error-unmatch docs/CLAIMS.md >/dev/null 2>&1; then
    echo "claims-gate: docs/CLAIMS.md must remain local and untracked" >&2
    exit 1
fi

if [[ ! -x "$MAKO" ]]; then
    echo "claims-gate: missing executable $MAKO (run cargo build --release first)" >&2
    exit 1
fi

expect_failure() {
    local label=$1
    shift
    set +e
    "$@" >"$TMP/$label.out" 2>&1
    local rc=$?
    set -e
    if [[ $rc -eq 0 ]]; then
        echo "claims-gate: $label unexpectedly succeeded" >&2
        cat "$TMP/$label.out" >&2
        exit 1
    fi
    echo "claims-gate: $label rejected (exit $rc)"
}

expect_failure gc-direct env MAKO_CACHE="$CACHE/gc-direct" \
    "$MAKO" check "$ROOT/examples/bad/gc_removed.mko"
expect_failure gc-config env MAKO_CACHE="$CACHE/gc-config" \
    "$MAKO" check "$ROOT/examples/bad/gc_removed_package/main.mko"
expect_failure unsafe-index env MAKO_CACHE="$CACHE/unsafe-index" \
    "$MAKO" check "$ROOT/examples/bad/unsafe_index_without_block.mko"
expect_failure immutable-index env MAKO_CACHE="$CACHE/immutable-index" \
    "$MAKO" check "$ROOT/examples/bad/index_assign_immutable.mko"
expect_failure immutable-field env MAKO_CACHE="$CACHE/immutable-field" \
    "$MAKO" check "$ROOT/examples/bad/field_assign_immutable.mko"
expect_failure temp-index env MAKO_CACHE="$CACHE/temp-index" \
    "$MAKO" check "$ROOT/examples/bad/index_assign_temporary.mko"
expect_failure temp-field env MAKO_CACHE="$CACHE/temp-field" \
    "$MAKO" check "$ROOT/examples/bad/field_assign_temporary.mko"
expect_failure arena-escape env MAKO_CACHE="$CACHE/arena-escape" \
    "$MAKO" check "$ROOT/examples/bad/arena_escape_return.mko"
expect_failure slice-view-escape env MAKO_CACHE="$CACHE/slice-view-escape" \
    "$MAKO" check "$ROOT/examples/bad/slice_view_escape.mko"
expect_failure slice-view-return env MAKO_CACHE="$CACHE/slice-view-return" \
    "$MAKO" check "$ROOT/examples/bad/slice_view_return.mko"
expect_failure arena-store-field env MAKO_CACHE="$CACHE/arena-store-field" \
    "$MAKO" check "$ROOT/examples/bad/arena_store_field.mko"
expect_failure race-mut-after-kick env MAKO_CACHE="$CACHE/race-mut-after-kick" \
    "$MAKO" check "$ROOT/examples/bad/race_mut_after_kick.mko"
expect_failure kick-mutable-closure env MAKO_CACHE="$CACHE/kick-mutable-closure" \
    "$MAKO" check "$ROOT/examples/bad/kick_mutable_closure_capture.mko"
expect_failure kick-mutable-lambda env MAKO_CACHE="$CACHE/kick-mutable-lambda" \
    "$MAKO" check "$ROOT/examples/bad/kick_mutable_lambda_capture.mko"
expect_failure fan-capture env MAKO_CACHE="$CACHE/fan-capture" \
    "$MAKO" check "$ROOT/examples/bad/fan_capture.mko"
env MAKO_CACHE="$CACHE/cookie-build" "$MAKO" build --release \
    "$ROOT/examples/bad/cookie_injection.mko" -o "$TMP/cookie-injection"
expect_failure cookie-injection "$TMP/cookie-injection"

# Safe bounds must remain enforced by an optimized binary.
env MAKO_CACHE="$CACHE/bounds-build" "$MAKO" build --release \
    "$ROOT/examples/bad/release_bounds_oob.mko" -o "$TMP/release-bounds-oob"
expect_failure release-bounds "$TMP/release-bounds-oob"

# Cloud Run output must not be created until a real image is supplied.
mkdir -p "$TMP/cloud-run"
expect_failure cloud-run-input env MAKO_CACHE="$CACHE/deploy" \
    "$MAKO" deploy serverless "$TMP/cloud-run" --provider cloud-run --name claims-gate
if find "$TMP/cloud-run" -type f -print -quit | grep -q .; then
    echo "claims-gate: cloud-run generated files without required image" >&2
    find "$TMP/cloud-run" -type f -print >&2
    exit 1
fi

if rg -n "MakoGcObj|mako_gc_arena_new" "$ROOT/runtime" "$ROOT/src"; then
    echo "claims-gate: tracing-GC implementation symbols remain" >&2
    exit 1
fi

# Removed no-op compatibility APIs must stay removed. Availability probes and
# SQL parameter placeholders are real contracts and are intentionally excluded.
if rg -n "hpack_decode_stub|tls_listen_stub|quic_stub|ws_echo_stub|grpc_stub_ping|queue_stub_ping|mako_sql_query\(" \
    "$ROOT/runtime" "$ROOT/src" "$ROOT/examples" "$ROOT/docs/BUILTINS.md"; then
    echo "claims-gate: removed placeholder API remains" >&2
    exit 1
fi

env MAKO_CACHE="$CACHE/full-suite" "$MAKO" test "$ROOT/examples/testing"
env MAKO_CACHE="$CACHE/cmap" "$MAKO" test "$ROOT/examples/testing/cmap_stress_test.mko"
MAKO_BIN="$MAKO" MAKO_CACHE="$CACHE/stdlib" "$ROOT/scripts/stdlib-gate.sh"

# A release cache must distinguish the documented default (-O3 -flto) from the
# explicit MAKO_NO_LTO opt-out.  This catches stale optimized objects being
# silently reused across materially different release builds.
OPT_CACHE="$CACHE/release-optimization"
MAKO_CACHE="$OPT_CACHE" MAKO_CACHE_LOG=1 "$MAKO" build --release \
    "$ROOT/examples/bench/micro.mko" -o "$TMP/release-default" \
    >"$TMP/release-default.log" 2>&1
MAKO_CACHE="$OPT_CACHE" MAKO_CACHE_LOG=1 MAKO_NO_LTO=1 "$MAKO" build --release \
    "$ROOT/examples/bench/micro.mko" -o "$TMP/release-no-lto" \
    >"$TMP/release-no-lto.log" 2>&1
if ! rg -q "object MISS" "$TMP/release-no-lto.log"; then
    echo "claims-gate: MAKO_NO_LTO reused default-LTO objects" >&2
    cat "$TMP/release-no-lto.log" >&2
    exit 1
fi

# When Zig is available, exercise the same deployable target contract as the
# required GitHub cross-artifact job. Native-only environments keep the rest
# of the claims gate usable without installing a foreign-target toolchain.
if command -v zig >/dev/null 2>&1; then
    CROSS_ROOT="$TMP/cross-artifacts"
    ZIG_LOCAL_CACHE_DIR="$CROSS_ROOT/zig-cache"
    export ZIG_LOCAL_CACHE_DIR
    mkdir -p "$CROSS_ROOT" "$ZIG_LOCAL_CACHE_DIR"
    MAKO_CACHE="$CACHE/cross-windows" "$MAKO" build --release \
        "$ROOT/examples/hello.mko" --target x86_64-pc-windows-gnu \
        -o "$CROSS_ROOT/hello.exe"
    "$ROOT/scripts/verify-target-artifact.sh" \
        x86_64-pc-windows-gnu "$CROSS_ROOT/hello.exe"
    MAKO_CACHE="$CACHE/cross-wasi" "$MAKO" build --release \
        "$ROOT/examples/hello.mko" --target wasm32-wasip1 \
        -o "$CROSS_ROOT/hello.wasm"
    "$ROOT/scripts/verify-target-artifact.sh" \
        wasm32-wasip1 "$CROSS_ROOT/hello.wasm"
    for target in x86_64-unknown-linux-musl aarch64-unknown-linux-musl; do
        MAKO_CACHE="$CACHE/cross-$target" "$MAKO" build --release \
            "$ROOT/examples/hello.mko" --target "$target" \
            -o "$CROSS_ROOT/$target"
        "$ROOT/scripts/verify-target-artifact.sh" \
            "$target" "$CROSS_ROOT/$target" --static
    done
else
    echo "claims-gate: cross-artifact checks skipped (zig not installed)"
fi
env MAKO_CACHE="$CACHE/identity" "$MAKO" lint --identity "$ROOT/examples/mako_style.mko"
MAKO_CACHE="$CACHE/bench" "$ROOT/scripts/bench-gate.sh"
MAKO_CACHE="$CACHE/bench-strict" MAKO_BENCH_STRICT=1 "$ROOT/scripts/bench-gate.sh"

echo "claims-gate: all claim checks passed"
