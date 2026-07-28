#!/usr/bin/env bash
# Publish a Mako package to the mako-packages GitHub Pages registry.
#
# Usage:
#   scripts/publish-registry.sh <path-to-package-dir>
#
# Prerequisites:
#   - Package must have a mako.toml with name and version at the top level
#   - gh CLI authenticated with push access to loreste/mako-packages
#   - Optional: MAKO_SIGN_KEY for ed25519 signing
#
# What this does:
#   1. Reads name/version from the package's mako.toml
#   2. Publishes locally (mako pkg publish) to get PACKAGE.sha256
#   3. Creates a tarball from the published package
#   4. Updates the index.json in mako-packages
#   5. Pushes to GitHub (triggers Pages deploy)

set -euo pipefail

PKG_DIR="${1:?usage: publish-registry.sh <package-dir>}"
PKG_DIR="$(cd "$PKG_DIR" && pwd)"

REGISTRY_REPO="${MAKO_PACKAGES_REPO:-loreste/mako-packages}"
REGISTRY_URL="${MAKO_PACKAGES_URL:-https://loreste.github.io/mako-packages}"
MAKO="${MAKO_BIN:-mako}"

# Read name and version from mako.toml
MANIFEST="$PKG_DIR/mako.toml"
if [ ! -f "$MANIFEST" ]; then
  echo "error: no mako.toml in $PKG_DIR" >&2
  exit 1
fi

NAME=$(grep -m1 '^name' "$MANIFEST" | sed 's/.*=\s*//; s/[" '\'']*//g')
VERSION=$(grep -m1 '^version' "$MANIFEST" | sed 's/.*=\s*//; s/[" '\'']*//g')

if [ -z "$NAME" ] || [ -z "$VERSION" ]; then
  echo "error: mako.toml must have top-level name and version" >&2
  exit 1
fi

echo "==> publishing $NAME@$VERSION"

# Step 1: publish locally to produce PACKAGE.sha256
LOCAL_REG=$(mktemp -d)
trap 'rm -rf "$LOCAL_REG" "$WORK_DIR"' EXIT
MAKO_REGISTRY="$LOCAL_REG" "$MAKO" pkg publish "$PKG_DIR"

PUBLISHED="$LOCAL_REG/$NAME/$VERSION"
if [ ! -d "$PUBLISHED" ]; then
  echo "error: local publish did not produce $PUBLISHED" >&2
  exit 1
fi

# Step 2: create tarball
TARBALL="$LOCAL_REG/$VERSION.tar.gz"
tar czf "$TARBALL" -C "$PUBLISHED" .
SHA=$(shasum -a 256 "$TARBALL" | awk '{print $1}')
echo "  sha256: $SHA"

# Step 3: clone registry, update, push
WORK_DIR=$(mktemp -d)
gh repo clone "$REGISTRY_REPO" "$WORK_DIR" -- --depth 1 2>/dev/null

PKG_DEST="$WORK_DIR/packages/$NAME"
mkdir -p "$PKG_DEST"
cp "$TARBALL" "$PKG_DEST/$VERSION.tar.gz"

# Build or update index.json
INDEX="$PKG_DEST/index.json"
ENTRY="\"$VERSION\": {\"sha256\": \"$SHA\", \"url\": \"$REGISTRY_URL/$NAME/$VERSION.tar.gz\"}"

if [ -f "$INDEX" ]; then
  # Insert new version entry before the closing of "versions"
  # Simple approach: rebuild from scratch by merging
  python3 -c "
import json, sys
with open('$INDEX') as f:
    idx = json.load(f)
idx.setdefault('versions', {})['$VERSION'] = {
    'sha256': '$SHA',
    'url': '$REGISTRY_URL/$NAME/$VERSION.tar.gz'
}
with open('$INDEX', 'w') as f:
    json.dump(idx, f, indent=2)
    f.write('\n')
"
else
  cat > "$INDEX" << ENDJSON
{
  "versions": {
    "$VERSION": {
      "sha256": "$SHA",
      "url": "$REGISTRY_URL/$NAME/$VERSION.tar.gz"
    }
  }
}
ENDJSON
fi

echo "  index: $(cat "$INDEX")"

# Step 4: commit and push
cd "$WORK_DIR"
git add "packages/$NAME"
git commit -m "publish $NAME@$VERSION"
git push

echo "==> published $NAME@$VERSION to $REGISTRY_URL"
echo "  fetch with: mako pkg get $NAME $VERSION"
