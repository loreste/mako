#!/usr/bin/env bash
# Build a simple .deb from a packaged release tarball or local install layout.
# Seed for P3 Linux packaging (not a full Debian package policy product).
#
# Usage:
#   cargo build --release
#   ./scripts/package-release.sh --slim
#   ./scripts/package-deb.sh
#
# Produces: dist/mako_<version>_<arch>.deb
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
VERSION="$(grep -m1 '^version' Cargo.toml | sed 's/.*"\(.*\)"/\1/')"
ARCH="$(uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/;s/arm64/arm64/')"
STAGE="dist/deb-stage"
PKG="mako_${VERSION}_${ARCH}"
rm -rf "$STAGE"
mkdir -p "$STAGE/DEBIAN" "$STAGE/usr/bin" "$STAGE/usr/share/mako"

BIN="target/release/mako"
if [[ ! -x "$BIN" ]]; then
  echo "error: missing $BIN — cargo build --release first" >&2
  exit 1
fi
install -m 755 "$BIN" "$STAGE/usr/bin/mako"
cp -R runtime "$STAGE/usr/share/mako/" 2>/dev/null || true
# headers only
mkdir -p "$STAGE/usr/share/mako/runtime"
install -m 644 runtime/*.h "$STAGE/usr/share/mako/runtime/"
if [[ -d runtime/certs ]]; then cp -R runtime/certs "$STAGE/usr/share/mako/runtime/"; fi
if [[ -d std ]]; then cp -R std "$STAGE/usr/share/mako/"; fi

cat > "$STAGE/DEBIAN/control" <<EOF
Package: mako
Version: $VERSION
Section: devel
Priority: optional
Architecture: $ARCH
Maintainer: Mako contributors <noreply@example.com>
Description: Mako language compiler (.mko → native via C)
 Homepage: https://github.com/loreste/mako
EOF

mkdir -p dist
if command -v dpkg-deb >/dev/null 2>&1; then
  dpkg-deb --build "$STAGE" "dist/${PKG}.deb"
  echo "Wrote dist/${PKG}.deb"
else
  # Portable fallback: tar.deb layout without dpkg
  tar -C "$STAGE" -czf "dist/${PKG}.tar.gz" .
  echo "dpkg-deb not found — wrote dist/${PKG}.tar.gz (stage layout)"
fi
