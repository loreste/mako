#!/usr/bin/env bash
# Lightweight install/doctor smoke for primary hosts (0.4.9 49-B).
# Uses an already-built release binary when present; otherwise builds without
# the llvm-backend feature (fast CI path).
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
mako_bin="${MAKO_BIN:-$repo_dir/target/release/mako}"

if [[ ! -x "$mako_bin" ]]; then
  echo "install-smoke: building release mako (no llvm-backend feature)"
  cargo build --release --manifest-path "$repo_dir/Cargo.toml"
fi

echo "install-smoke: version"
"$mako_bin" --version

echo "install-smoke: doctor"
"$mako_bin" doctor

echo "install-smoke: init + run"
tmp="${RUNNER_TEMP:-${TMPDIR:-/tmp}}/mako-install-smoke-$$"
rm -rf "$tmp"
"$mako_bin" init "$tmp" --name install-smoke
"$mako_bin" run "$tmp/main.mko"
rm -rf "$tmp"

echo "install-smoke: ok"
