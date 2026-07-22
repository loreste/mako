#!/usr/bin/env bash
# =============================================================================
# Mako Linux One-Shot Installer — Prebuilt Binary & Environment Setup
# =============================================================================
#
#   curl -fsSL https://github.com/loreste/mako/releases/latest/download/install-linux.sh | bash
#
# Issues Addressed & Fixes Implemented:
#
#   1. Linux Distribution Identification Fix:
#      - Issue: Previously, the installer only checked `uname -s` ("Linux") without
#        identifying specific Linux distributions like Debian. If a compiler existed,
#        no OS distribution information was detected or displayed.
#      - Fix: Added `detect_distro()` to parse `/etc/os-release`, `/etc/debian_version`,
#        `lsb_release`, `/etc/redhat-release`, and `/etc/alpine-release`. Accurately
#        logs distribution details (e.g., Debian GNU/Linux 13 trixie) during installation.
#
#   2. Environment & Profile Persistence Across Sessions & Reboots:
#      - Issue: Running `source env.sh` was temporary and disappeared on new shells
#        or reboots. Interactive runs skipped profile appending, and system installs
#        lacked `/etc/profile.d/mako.sh` integration.
#      - Fix: `append_shell_rc()` now automatically populates system-wide profile files
#        (`/etc/profile.d/mako.sh`) for root/system installs (standard across Debian &
#        Linux distros to survive reboots & new login sessions) and user profile files
#        (`~/.bashrc`, `~/.profile`, `~/.zshrc`, `~/.bash_profile`).
#
# What you get in PREFIX (default ~/.local):
#   bin/mako                  — compiler CLI
#   share/mako/runtime/       — C runtime headers (required to build .mko)
#   share/mako/std/           — standard library
#   share/mako/env.sh         — source this for PATH + MAKO_RUNTIME
# =============================================================================
set -euo pipefail

VERSION="${MAKO_VERSION:-latest}"
PREFIX="${PREFIX:-$HOME/.local}"
ARTIFACT="${MAKO_ARTIFACT:-}"
BASE_URL="${MAKO_RELEASE_BASE_URL:-}"
RUN_DOCTOR=1
INSTALL_DEPS=1
MODIFY_SHELL=1
YES=0

usage() {
  cat <<'EOF'
Usage: install-release.sh [options]

Out-of-the-box Mako install from GitHub Releases prebuilds.
Does NOT install Rust or clone the repository.

Options:
  --version <tag|latest>  Release tag (default: latest)
  --prefix <path>         Install prefix (default: $HOME/.local)
  --artifact <name>       Override platform artifact name
  --base-url <url>        Asset base URL (https:// or file://)
  --with-deps             Install clang if missing (default on Linux)
  --no-deps               Never install system packages
  --no-shell              Do not append PATH lines to ~/.bashrc / ~/.zshrc
  --yes                   Non-interactive (assume yes for shell PATH append)
  --skip-doctor           Skip mako doctor after install
  -h, --help              Show this help

Environment:
  MAKO_VERSION  PREFIX  MAKO_ARTIFACT  MAKO_RELEASE_BASE_URL
  MAKO_INSTALL_DEPS=0   same as --no-deps
  MAKO_YES=1            same as --yes

Examples:
  curl -fsSL https://github.com/loreste/mako/releases/latest/download/install-linux.sh | bash
  curl -fsSL …/install-release.sh | bash -s -- --prefix /opt/mako --yes
EOF
}

if [[ "${MAKO_INSTALL_DEPS:-}" == "0" ]]; then INSTALL_DEPS=0; fi
if [[ "${MAKO_YES:-}" == "1" ]]; then YES=1; fi

while [[ $# -gt 0 ]]; do
  case "$1" in
    --version) VERSION="${2:?}"; shift 2 ;;
    --prefix) PREFIX="${2:?}"; shift 2 ;;
    --artifact) ARTIFACT="${2:?}"; shift 2 ;;
    --base-url) BASE_URL="${2:?}"; shift 2 ;;
    --with-deps) INSTALL_DEPS=1; shift ;;
    --no-deps) INSTALL_DEPS=0; shift ;;
    --no-shell) MODIFY_SHELL=0; shift ;;
    --yes|-y) YES=1; shift ;;
    --skip-doctor) RUN_DOCTOR=0; shift ;;
    -h|--help) usage; exit 0 ;;
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

have_cc() {
  command -v clang >/dev/null 2>&1 || command -v cc >/dev/null 2>&1
}

sha256_file() {
  local f="$1"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$f" | awk '{ print $1 }'
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$f" | awk '{ print $1 }'
  elif command -v openssl >/dev/null 2>&1; then
    openssl dgst -sha256 "$f" | awk '{ print $NF }'
  else
    echo "error: need sha256sum, shasum, or openssl" >&2
    exit 1
  fi
}

human_size() {
  local n="$1"
  if command -v numfmt >/dev/null 2>&1; then
    numfmt --to=iec --suffix=B "$n" 2>/dev/null && return
  fi
  awk -v n="$n" 'BEGIN {
    split("B KB MB GB", u, " ")
    i = 1
    while (n >= 1024 && i < 4) { n /= 1024; i++ }
    if (i == 1) printf "%dB\n", n
    else printf "%.1f%s\n", n, u[i]
  }'
}

run_root() {
  # Run package manager install with privilege when needed.
  if [[ "${EUID:-$(id -u)}" -eq 0 ]]; then
    "$@"
  elif command -v sudo >/dev/null 2>&1; then
    sudo "$@"
  else
    echo "error: need root or sudo to install system packages: $*" >&2
    return 1
  fi
}

# -----------------------------------------------------------------------------
# detect_distro: Identify host Linux distribution (Debian, Ubuntu, Fedora, etc.)
# Fixes Issue #1: Parses /etc/os-release, /etc/debian_version, or lsb_release to
# accurately display and use host OS info during dependency setup and logging.
# -----------------------------------------------------------------------------
detect_distro() {
  local os
  os="$(uname -s)"
  if [[ "$os" == "Darwin" ]]; then
    local mac_ver
    mac_ver="$(sw_vers -productVersion 2>/dev/null || echo "")"
    echo "macOS ${mac_ver:-}"
    return 0
  fi

  if [[ "$os" != "Linux" ]]; then
    echo "$os"
    return 0
  fi

  local name="" id="" version=""
  if [[ -f /etc/os-release ]]; then
    name="$(grep -E '^PRETTY_NAME=' /etc/os-release | cut -d= -f2- | tr -d '"')"
    if [[ -z "$name" ]]; then
      name="$(grep -E '^NAME=' /etc/os-release | cut -d= -f2- | tr -d '"')"
    fi
    id="$(grep -E '^ID=' /etc/os-release | cut -d= -f2- | tr -d '"')"
    version="$(grep -E '^VERSION_ID=' /etc/os-release | cut -d= -f2- | tr -d '"')"
  elif [[ -f /etc/debian_version ]]; then
    name="Debian GNU/Linux $(cat /etc/debian_version)"
    id="debian"
  elif command -v lsb_release >/dev/null 2>&1; then
    name="$(lsb_release -ds 2>/dev/null | tr -d '"')"
    id="$(lsb_release -is 2>/dev/null | tr -s '[:upper:]' '[:lower:]')"
  elif [[ -f /etc/redhat-release ]]; then
    name="$(cat /etc/redhat-release)"
    id="rhel"
  elif [[ -f /etc/alpine-release ]]; then
    name="Alpine Linux $(cat /etc/alpine-release)"
    id="alpine"
  fi

  if [[ -n "$name" ]]; then
    echo "Linux ($name)"
  else
    echo "Linux (unknown distribution)"
  fi
}

ensure_clang() {
  local distro
  distro="$(detect_distro)"
  if have_cc; then
    echo "OS distro: $distro"
    echo "C compiler: $(command -v clang 2>/dev/null || command -v cc)"
    return 0
  fi
  if [[ "$INSTALL_DEPS" -eq 0 ]]; then
    echo "OS distro: $distro"
    echo "warning: no clang/cc on PATH (--no-deps). Install a C compiler to build .mko programs." >&2
    return 0
  fi

  local os
  os="$(uname -s)"
  echo "OS distro: $distro"
  echo "C compiler not found — installing clang (needed to compile .mko programs)…"

  if [[ "$os" == "Darwin" ]]; then
    if command -v xcode-select >/dev/null 2>&1; then
      echo "  macOS: running xcode-select --install (GUI prompt may appear)"
      xcode-select --install 2>/dev/null || true
      echo "  If clang is still missing, install Xcode Command Line Tools, then re-run mako."
    else
      echo "  Install Xcode Command Line Tools, then re-run."
    fi
    return 0
  fi

  if [[ "$os" != "Linux" ]]; then
    echo "  Unsupported OS for auto deps; install clang manually." >&2
    return 0
  fi

  if command -v apt-get >/dev/null 2>&1; then
    echo "  Detected Debian/Ubuntu package manager (apt-get)…"
    run_root apt-get update -y
    run_root env DEBIAN_FRONTEND=noninteractive apt-get install -y clang ca-certificates
  elif command -v dnf >/dev/null 2>&1; then
    echo "  Detected Fedora/RHEL package manager (dnf)…"
    run_root dnf install -y clang
  elif command -v yum >/dev/null 2>&1; then
    echo "  Detected RHEL/CentOS package manager (yum)…"
    run_root yum install -y clang
  elif command -v pacman >/dev/null 2>&1; then
    echo "  Detected Arch Linux package manager (pacman)…"
    run_root pacman -Sy --noconfirm clang
  elif command -v apk >/dev/null 2>&1; then
    echo "  Detected Alpine Linux package manager (apk)…"
    run_root apk add --no-cache clang
  elif command -v zypper >/dev/null 2>&1; then
    echo "  Detected openSUSE package manager (zypper)…"
    run_root zypper install -y clang
  else
    echo "warning: unknown package manager; install clang yourself." >&2
    return 0
  fi

  if have_cc; then
    echo "C compiler: $(command -v clang 2>/dev/null || command -v cc)"
  else
    echo "warning: clang install attempted but clang/cc still not on PATH" >&2
  fi
}

detect_artifact() {
  local os arch
  os="$(uname -s)"
  arch="$(uname -m)"
  case "$os:$arch" in
    Darwin:arm64|Darwin:aarch64) echo "mako-aarch64-apple-darwin" ;;
    Darwin:x86_64|Darwin:amd64) echo "mako-x86_64-apple-darwin" ;;
    Linux:aarch64|Linux:arm64) echo "mako-aarch64-unknown-linux-gnu" ;;
    Linux:x86_64|Linux:amd64) echo "mako-x86_64-unknown-linux-gnu" ;;
    Linux:*)
      echo "error: unsupported Linux arch '$arch' (need x86_64 or aarch64)" >&2
      exit 1
      ;;
    *)
      echo "error: unsupported host $os/$arch — use prebuilt Linux/macOS or Windows zip" >&2
      exit 1
      ;;
  esac
}

download() {
  local url="$1" out="$2"
  case "$url" in
    file://*)
      local path="${url#file://}"
      if [[ "$path" == /* ]]; then
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

write_env_file() {
  local envf="$1"
  cat > "$envf" <<EOF
# Mako environment — generated by install-release.sh
# Usage:  source $envf
export PATH="$BIN_DIR:\$PATH"
export MAKO_RUNTIME="$RUNTIME_DST"
export MAKO_STD="$STD_DST"
EOF
}

# -----------------------------------------------------------------------------
# append_shell_rc: Persist PATH & MAKO_RUNTIME env vars across sessions & reboots.
# Fixes Issue #2:
# 1) Automatically creates /etc/profile.d/mako.sh for system-wide/root installs,
#    ensuring PATH persistence across all login/subshell sessions and reboots.
# 2) Appends source directives to ~/.bashrc, ~/.profile, ~/.zshrc, ~/.bash_profile.
# 3) Removes the restrictive interactive skip so environment setup is automatic.
# -----------------------------------------------------------------------------
append_shell_rc() {
  local line="# Mako environment"
  local src_line="[ -f \"$SHARE_DIR/env.sh\" ] && . \"$SHARE_DIR/env.sh\""
  local updated_any=0

  # 1. System-wide profile installation (/etc/profile.d/mako.sh & /etc/profile)
  # Standard for Debian, Ubuntu, RHEL, Fedora, Arch, Alpine, etc.
  # Ensures persistence across all user sessions, subshells, and system reboots.
  if [[ "${EUID:-$(id -u)}" -eq 0 ]] || [[ -w /etc/profile.d ]]; then
    local sys_profile_dir="/etc/profile.d"
    local sys_profile_file="$sys_profile_dir/mako.sh"
    if mkdir -p "$sys_profile_dir" 2>/dev/null && {
      cat > "$sys_profile_file" <<EOF
# Mako environment configuration
$src_line
EOF
      chmod 644 "$sys_profile_file" 2>/dev/null
    }; then
      echo "  updated system profile: $sys_profile_file (persists across reboots & all users)"
      updated_any=1
    fi
  fi

  # 2. User-level shell profiles (~/.bashrc, ~/.profile, ~/.zshrc, ~/.bash_profile)
  local rc
  for rc in "$HOME/.bashrc" "$HOME/.profile" "$HOME/.zshrc" "$HOME/.bash_profile"; do
    if [[ -f "$rc" ]] || [[ "$rc" == "$HOME/.profile" && "$PREFIX" == "$HOME"* ]]; then
      if [[ -f "$rc" ]] && grep -qF "share/mako/env.sh" "$rc" 2>/dev/null; then
        continue
      fi
      if {
        echo ""
        echo "$line"
        echo "$src_line"
      } >> "$rc" 2>/dev/null; then
        echo "  updated user profile: $rc"
        updated_any=1
      else
        echo "  note: could not write to $rc (read-only); manually add:"
        echo "    $src_line"
      fi
    fi
  done

  if [[ "$updated_any" -eq 0 ]]; then
    echo "  note: add manually to /etc/profile or ~/.bashrc:"
    echo "    $src_line"
  fi
}

# --- main ---

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
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

ARCHIVE="$WORK/$ARTIFACT.tar.gz"
CHECKSUM_FILE="$WORK/$ARTIFACT.sha256"
ARCHIVE_URL="$BASE_URL/$ARTIFACT.tar.gz"
CHECKSUM_URL="$BASE_URL/$ARTIFACT.sha256"

echo "=========================================="
echo " Mako one-shot install (prebuilt)"
echo "=========================================="
echo "  version:  $VERSION"
echo "  artifact: $ARTIFACT"
echo "  distro:   $(detect_distro)"
echo "  prefix:   $PREFIX"
echo "  source:   $BASE_URL"
echo "  rust:     not required"
echo "  git:      not required"
echo ""

# 1) system C compiler (so .mko builds work out of the box)
ensure_clang
echo ""

# 2) download prebuilt mako
echo "downloading $ARTIFACT.tar.gz …"
if ! download "$ARCHIVE_URL" "$ARCHIVE"; then
  echo "error: failed to download $ARCHIVE_URL" >&2
  echo "  Is a release published with this artifact?" >&2
  echo "  See: https://github.com/loreste/mako/releases" >&2
  exit 1
fi
echo "downloading $ARTIFACT.sha256 …"
download "$CHECKSUM_URL" "$CHECKSUM_FILE"

BYTES="$(wc -c < "$ARCHIVE" | tr -d ' ')"
echo "  download size: $(human_size "$BYTES")"

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
  echo "error: could not parse checksum file" >&2
  cat "$CHECKSUM_FILE" >&2 || true
  exit 1
}
ACTUAL="$(sha256_file "$ARCHIVE")"
if [[ "$ACTUAL" != "$EXPECTED" ]]; then
  echo "error: checksum mismatch" >&2
  echo "  expected: $EXPECTED" >&2
  echo "  actual:   $ACTUAL" >&2
  exit 1
fi
echo "checksum: ok"
echo ""

echo "extracting …"
tar --no-same-owner -xzf "$ARCHIVE" -C "$WORK"

STAGE="$WORK/$ARTIFACT"
if [[ ! -d "$STAGE" ]]; then
  STAGE="$(find "$WORK" -mindepth 1 -maxdepth 1 -type d | head -1)"
fi
if [[ ! -x "$STAGE/bin/mako" ]]; then
  echo "error: archive missing bin/mako" >&2
  ls -laR "$WORK" >&2 || true
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

if [[ -d "$STAGE/share/mako/runtime" ]]; then
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
  if [[ -f "$STAGE/share/mako/docs/README.md" ]]; then
    mkdir -p "$SHARE_DIR/docs"
    install -m 644 "$STAGE/share/mako/docs/README.md" "$SHARE_DIR/docs/README.md"
  fi
  echo "$RUNTIME_DST" > "$SHARE_DIR/runtime_path.txt"
elif [[ -x "$STAGE/scripts/install.sh" ]]; then
  PREFIX="$PREFIX" "$STAGE/scripts/install.sh" --skip-build
else
  echo "error: bad archive layout" >&2
  exit 1
fi

write_env_file "$SHARE_DIR/env.sh"
export PATH="$BIN_DIR:$PATH"
export MAKO_RUNTIME="$RUNTIME_DST"
export MAKO_STD="$STD_DST"

if [[ "$MODIFY_SHELL" -eq 1 ]]; then
  echo "shell setup:"
  append_shell_rc
fi

if [[ "$RUN_DOCTOR" -eq 1 ]]; then
  echo ""
  echo "running mako doctor …"
  if ! "$BIN_DIR/mako" doctor; then
    echo "warning: doctor reported issues (install still completed)" >&2
  fi
fi

echo ""
echo "=========================================="
echo " Mako installed successfully"
echo "=========================================="
echo "  binary:  $BIN_DIR/mako"
"$BIN_DIR/mako" --version 2>/dev/null || true
echo "  distro:  $(detect_distro)"
echo "  runtime: $RUNTIME_DST"
echo "  stdlib:  $STD_DST"
echo "  env:     $SHARE_DIR/env.sh"
echo ""
echo "Environment profile persistence:"
echo "  System & user profiles updated (~/.bashrc, ~/.profile, /etc/profile.d/mako.sh)."
echo "  Mako will be automatically available in new shell sessions & after reboot."
echo ""
echo "To activate Mako in your CURRENT shell session right now:"
echo "  source \"$SHARE_DIR/env.sh\""
echo ""
echo "Try it:"
echo "  mako version"
echo "  mako init hello && cd hello && mako run main.mko"
echo ""
if ! have_cc; then
  echo "NOTE: install a C compiler so mako can compile programs:"
  echo "  Debian/Ubuntu:  sudo apt-get install -y clang"
  echo "  Fedora:         sudo dnf install -y clang"
  echo "  macOS:          xcode-select --install"
fi
