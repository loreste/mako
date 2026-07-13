#!/usr/bin/env bash
# One-shot Mako release installer (prebuilt — no Rust, no source clone).
#
#   curl -fsSL https://github.com/loreste/mako/releases/latest/download/install-release.sh | bash
#   curl -fsSL …/install-release.sh | bash -s -- --version v0.1.0 --prefix "$HOME/.local"
#
# Downloads one platform tarball (+ tiny .sha256), verifies, installs into PREFIX.
set -euo pipefail

VERSION="${MAKO_VERSION:-latest}"
PREFIX="${PREFIX:-$HOME/.local}"
ARTIFACT="${MAKO_ARTIFACT:-}"
BASE_URL="${MAKO_RELEASE_BASE_URL:-}"
RUN_DOCTOR=1
ADD_PATH_HINT=1

usage() {
  cat <<'EOF'
Usage: install-release.sh [options]

One-shot install of a prebuilt Mako release (compiler + runtime headers + std).
Does NOT download Rust, cargo crates, or a git clone.

Options:
  --version <tag|latest>  Release tag (default: latest)
  --prefix <path>         Install prefix (default: $HOME/.local)
  --artifact <name>       Override auto-detected platform artifact
  --base-url <url>        Asset base URL (https:// or file://)
  --skip-doctor           Skip `mako doctor` after install
  -h, --help              Show this help

Environment:
  MAKO_VERSION, PREFIX, MAKO_ARTIFACT, MAKO_RELEASE_BASE_URL

Examples:
  curl -fsSL https://github.com/loreste/mako/releases/latest/download/install-release.sh | bash
  PREFIX=/opt/mako bash install-release.sh --version v0.1.0
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --version)
      VERSION="${2:?missing value for --version}"
      shift 2
      ;;
    --prefix)
      PREFIX="${2:?missing value for --prefix}"
      shift 2
      ;;
    --artifact)
      ARTIFACT="${2:?missing value for --artifact}"
      shift 2
      ;;
    --base-url)
      BASE_URL="${2:?missing value for --base-url}"
      shift 2
      ;;
    --skip-doctor)
      RUN_DOCTOR=0
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "error: unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "error: missing required command: $1" >&2
    exit 1
  }
}

# Portable SHA-256 of a file → hex digest only.
sha256_file() {
  local f="$1"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$f" | awk '{ print $1 }'
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$f" | awk '{ print $1 }'
  elif command -v openssl >/dev/null 2>&1; then
    openssl dgst -sha256 "$f" | awk '{ print $NF }'
  else
    echo "error: need sha256sum, shasum, or openssl to verify the download" >&2
    exit 1
  fi
}

detect_artifact() {
  local os arch
  os="$(uname -s)"
  arch="$(uname -m)"
  case "$os:$arch" in
    Darwin:arm64|Darwin:aarch64) echo "mako-aarch64-apple-darwin" ;;
    Darwin:x86_64|Darwin:i386|Darwin:amd64) echo "mako-x86_64-apple-darwin" ;;
    Linux:aarch64|Linux:arm64) echo "mako-aarch64-unknown-linux-gnu" ;;
    Linux:x86_64|Linux:amd64) echo "mako-x86_64-unknown-linux-gnu" ;;
    Linux:*)
      echo "error: unsupported Linux arch '$arch' (need x86_64 or aarch64)" >&2
      echo "  pass --artifact explicitly if you have a custom build" >&2
      exit 1
      ;;
    *)
      echo "error: unsupported host $os/$arch" >&2
      echo "  Linux/macOS prebuilts only. On Windows use the .zip release or install.ps1" >&2
      exit 1
      ;;
  esac
}

download() {
  local url="$1" out="$2"
  case "$url" in
    file://*)
      local path="${url#file://}"
      # file:///abs → /abs ; file://rel stays relative
      if [[ "$path" == /* || "$path" == ./* || "$path" == ../* ]]; then
        cp "$path" "$out"
      else
        cp "/$path" "$out" 2>/dev/null || cp "$path" "$out"
      fi
      ;;
    http://*|https://*)
      curl -fsSL --connect-timeout 30 --retry 3 --retry-delay 1 "$url" -o "$out"
      ;;
    *)
      cp "$url" "$out"
      ;;
  esac
}

human_size() {
  local n="$1"
  if command -v numfmt >/dev/null 2>&1; then
    numfmt --to=iec --suffix=B "$n" 2>/dev/null && return
  fi
  # Portable fallback (awk)
  awk -v n="$n" 'BEGIN {
    split("B KB MB GB", u, " ")
    i = 1
    while (n >= 1024 && i < 4) { n /= 1024; i++ }
    if (i == 1) printf "%dB\n", n
    else printf "%.1f%s\n", n, u[i]
  }'
}

if [[ -z "$ARTIFACT" ]]; then
  ARTIFACT="$(detect_artifact)"
fi

if [[ -z "$BASE_URL" ]]; then
  if [[ "$VERSION" == "latest" ]]; then
    BASE_URL="https://github.com/loreste/mako/releases/latest/download"
  else
    BASE_URL="https://github.com/loreste/mako/releases/download/$VERSION"
  fi
fi
BASE_URL="${BASE_URL%/}"

need_cmd tar
need_cmd awk
need_cmd uname
if [[ "$BASE_URL" == http://* || "$BASE_URL" == https://* ]]; then
  need_cmd curl
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/mako-install.XXXXXX")"
cleanup() {
  rm -rf "$WORK"
}
trap cleanup EXIT

ARCHIVE="$WORK/$ARTIFACT.tar.gz"
CHECKSUM_FILE="$WORK/$ARTIFACT.sha256"
ARCHIVE_URL="$BASE_URL/$ARTIFACT.tar.gz"
CHECKSUM_URL="$BASE_URL/$ARTIFACT.sha256"

echo "mako one-shot install (prebuilt)"
echo "  version:  $VERSION"
echo "  artifact: $ARTIFACT"
echo "  prefix:   $PREFIX"
echo "  source:   $BASE_URL"
echo "  note:     no Rust / no git clone — single tarball download"

echo "downloading $ARTIFACT.tar.gz …"
download "$ARCHIVE_URL" "$ARCHIVE"
echo "downloading $ARTIFACT.sha256 …"
download "$CHECKSUM_URL" "$CHECKSUM_FILE"

BYTES="$(wc -c < "$ARCHIVE" | tr -d ' ')"
echo "  size:     $(human_size "$BYTES")"

EXPECTED="$(
  awk -v f="$ARTIFACT.tar.gz" '
    $2 == f || $2 == ("*" f) || $2 == ("./" f) { print $1; found=1; exit }
    NF == 1 { only=$1 }
    END {
      if (found) exit 0
      if (only != "") { print only; exit 0 }
      exit 1
    }
  ' "$CHECKSUM_FILE"
)" || {
  echo "error: could not parse checksum for $ARTIFACT.tar.gz" >&2
  echo "---- checksum file ----" >&2
  cat "$CHECKSUM_FILE" >&2 || true
  exit 1
}
ACTUAL="$(sha256_file "$ARCHIVE")"
if [[ "$ACTUAL" != "$EXPECTED" ]]; then
  echo "error: checksum mismatch for $ARTIFACT.tar.gz" >&2
  echo "  expected: $EXPECTED" >&2
  echo "  actual:   $ACTUAL" >&2
  exit 1
fi
echo "checksum: ok"

echo "extracting …"
tar -xzf "$ARCHIVE" -C "$WORK"

STAGE="$WORK/$ARTIFACT"
if [[ ! -d "$STAGE" ]]; then
  # Some packagers flatten; accept a single top-level dir.
  STAGE="$(find "$WORK" -mindepth 1 -maxdepth 1 -type d | head -1)"
fi
if [[ ! -x "$STAGE/bin/mako" ]]; then
  echo "error: archive missing bin/mako (layout: bin/ + share/mako/)" >&2
  ls -la "$WORK" >&2 || true
  exit 1
fi

BIN_DIR="$PREFIX/bin"
SHARE_DIR="$PREFIX/share/mako"
RUNTIME_DST="$SHARE_DIR/runtime"
STD_DST="$SHARE_DIR/std"
EDITORS_DST="$SHARE_DIR/editors"

echo "installing into $PREFIX …"
mkdir -p "$BIN_DIR" "$RUNTIME_DST" "$STD_DST"
install -m 755 "$STAGE/bin/mako" "$BIN_DIR/mako"

# Prefer staged share/ layout; fall back to nested scripts/install if present.
if [[ -d "$STAGE/share/mako/runtime" ]]; then
  # Headers only
  shopt -s nullglob
  for h in "$STAGE/share/mako/runtime"/*.h; do
    install -m 644 "$h" "$RUNTIME_DST/"
  done
  if [[ -d "$STAGE/share/mako/runtime/certs" ]]; then
    mkdir -p "$RUNTIME_DST/certs"
    cp -R "$STAGE/share/mako/runtime/certs/." "$RUNTIME_DST/certs/"
  fi
  if [[ -d "$STAGE/share/mako/std" ]]; then
    rm -rf "$STD_DST"
    mkdir -p "$STD_DST"
    cp -R "$STAGE/share/mako/std/." "$STD_DST/"
  fi
  if [[ -d "$STAGE/share/mako/editors" ]]; then
    rm -rf "$EDITORS_DST"
    mkdir -p "$EDITORS_DST"
    cp -R "$STAGE/share/mako/editors/." "$EDITORS_DST/"
  fi
  # Minimal docs (README only) if present
  if [[ -f "$STAGE/share/mako/docs/README.md" ]]; then
    mkdir -p "$SHARE_DIR/docs"
    install -m 644 "$STAGE/share/mako/docs/README.md" "$SHARE_DIR/docs/README.md"
  fi
  echo "$RUNTIME_DST" > "$SHARE_DIR/runtime_path.txt"
elif [[ -x "$STAGE/scripts/install.sh" ]]; then
  PREFIX="$PREFIX" "$STAGE/scripts/install.sh" --skip-build
else
  echo "error: release archive has no share/mako/runtime or scripts/install.sh" >&2
  exit 1
fi

# Ensure user can find the binary without a full login shell reload.
export PATH="$BIN_DIR:$PATH"

if ! command -v clang >/dev/null 2>&1 && ! command -v cc >/dev/null 2>&1; then
  echo ""
  echo "warning: no C compiler on PATH (clang/cc)."
  echo "  Mako compiles .mko → C → native binary; install a compiler:"
  echo "    Debian/Ubuntu:  sudo apt-get install -y clang"
  echo "    Fedora:         sudo dnf install -y clang"
  echo "    Arch:           sudo pacman -S clang"
  echo "    Alpine:         sudo apk add clang"
fi

if [[ "$RUN_DOCTOR" -eq 1 ]]; then
  echo "running mako doctor …"
  if ! MAKO_RUNTIME="$RUNTIME_DST" MAKO_STD="$STD_DST" "$BIN_DIR/mako" doctor; then
    echo "warning: mako doctor reported issues (install still completed)" >&2
  fi
fi

echo ""
echo "Installed: $BIN_DIR/mako"
"$BIN_DIR/mako" --version 2>/dev/null || true
echo "Runtime:   $RUNTIME_DST"
echo "Stdlib:    $STD_DST"
if [[ "$ADD_PATH_HINT" -eq 1 ]]; then
  case ":$PATH:" in
    *":$BIN_DIR:"*) ;;
    *)
      echo ""
      echo "Add to PATH (this shell):"
      echo "  export PATH=\"$BIN_DIR:\$PATH\""
      if [[ -f "$HOME/.bashrc" ]]; then
        echo "Persist for bash:"
        echo "  echo 'export PATH=\"$BIN_DIR:\$PATH\"' >> ~/.bashrc"
      fi
      if [[ -f "$HOME/.zshrc" ]]; then
        echo "Persist for zsh:"
        echo "  echo 'export PATH=\"$BIN_DIR:\$PATH\"' >> ~/.zshrc"
      fi
      ;;
  esac
fi
echo ""
echo "Quick check:"
echo "  mako version"
echo "  mako init hello && cd hello && mako run main.mko"
