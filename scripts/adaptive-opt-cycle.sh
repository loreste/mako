#!/usr/bin/env bash
# Adaptive optimization cycle (docs/ADAPTIVE_OPT.md).
#
# Never rewrites a running binary. Flow:
#   1) Release AOT build (Layer A)
#   2) Optional short train run that can export hot-site JSON guidance
#   3) Offline PGO rebuild via pgo-build.sh (Layer C)
#
# Usage:
#   ./scripts/adaptive-opt-cycle.sh examples/bench/http_long_run_server.mko -o out/app
#   ./scripts/adaptive-opt-cycle.sh app.mko -o out/app -- 2000 19820
#
# Env:
#   MAKO_BIN            — compiler (default target/release/mako)
#   MAKO_ADAPTIVE_SKIP_PGO=1  — only do release AOT + guidance note
#   MAKO_PGO_*          — forwarded to pgo-build.sh
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
mako_bin="${MAKO_BIN:-$repo_dir/target/release/mako}"

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

mkdir -p "$(dirname "$out")"
guide_dir="${MAKO_ADAPTIVE_GUIDE_DIR:-$repo_dir/out/adaptive-guide}"
mkdir -p "$guide_dir"

echo "adaptive-opt: [1/3] release AOT → $out.aot"
"$mako_bin" build "$src" --release --no-incremental -o "$out.aot"

echo "adaptive-opt: [2/3] guidance note (hot_site / profile are in-process APIs)"
cat >"$guide_dir/README.txt" <<EOF
Adaptive opt guidance (no online JIT)
=====================================
- Ship $out.aot (or the PGO binary below) — full native from t=0.
- In production, call hot_site_enable(1) and hot_site_hit(id) on routes.
- Scrape GET /debug/hot_sites (profile_http_route) or hot_sites_json().
- Never enable MAKO_PGO_GEN on the live years-up process (instrumentation tax).
- Re-train offline with scripts/pgo-build.sh under representative load.
- Blue/green the new binary; do not patch code in the running process.

See docs/ADAPTIVE_OPT.md
EOF
echo "adaptive-opt: wrote $guide_dir/README.txt"

if [[ "${MAKO_ADAPTIVE_SKIP_PGO:-0}" == "1" ]]; then
  cp -f "$out.aot" "$out"
  echo "adaptive-opt: skip PGO (MAKO_ADAPTIVE_SKIP_PGO=1) → $out"
  ls -la "$out"
  exit 0
fi

echo "adaptive-opt: [3/3] offline PGO rebuild → $out"
if [[ ${#train_args[@]} -gt 0 ]]; then
  "$repo_dir/scripts/pgo-build.sh" "$src" -o "$out" -- "${train_args[@]}"
else
  "$repo_dir/scripts/pgo-build.sh" "$src" -o "$out"
fi

echo "adaptive-opt: done"
ls -la "$out" "$out.aot" 2>/dev/null || ls -la "$out"
