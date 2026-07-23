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

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) ;;
  *)
    echo "install-smoke: installed-prefix native runtime"
    install_root="${RUNNER_TEMP:-${TMPDIR:-/tmp}}/mako-install-prefix-$$"
    PREFIX="$install_root" "$repo_dir/scripts/install.sh" --skip-build
    test -f "$install_root/share/mako/runtime/native_runtime.c"
    test -f "$install_root/share/mako/runtime/native_bridge.c"
    MAKO_RUNTIME="$install_root/share/mako/runtime" \
      "$install_root/bin/mako" build \
      "$repo_dir/examples/native/native_if_expr.mko" --backend native \
      -o "$install_root/native-if-expr"
    "$install_root/native-if-expr" >/dev/null
    rm -rf "$install_root"
    ;;
esac

echo "install-smoke: init + run"
tmp="${RUNNER_TEMP:-${TMPDIR:-/tmp}}/mako-install-smoke-$$"
rm -rf "$tmp"
"$mako_bin" init "$tmp" --name install-smoke
"$mako_bin" run "$tmp/main.mko"
rm -rf "$tmp"

echo "install-smoke: ok"
