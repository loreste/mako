#!/usr/bin/env bash
# RPM packaging seed (P3). Builds a simple rpm when rpmbuild is available.
#
# Usage:
#   cargo build --release
#   ./scripts/package-rpm.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
VERSION="$(grep -m1 '^version' Cargo.toml | sed 's/.*"\(.*\)"/\1/')"
ARCH="$(uname -m)"
TOP="dist/rpmbuild"
rm -rf "$TOP"
mkdir -p "$TOP"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

BIN="target/release/mako"
if [[ ! -x "$BIN" ]]; then
  echo "error: missing $BIN — cargo build --release first" >&2
  exit 1
fi

# Stage payload as tarball for %install
STAGE="dist/rpm-stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/usr/bin" "$STAGE/usr/share/mako/runtime"
install -m 755 "$BIN" "$STAGE/usr/bin/mako"
install -m 644 runtime/*.h "$STAGE/usr/share/mako/runtime/"
[[ -d std ]] && cp -R std "$STAGE/usr/share/mako/"
tar -C "$STAGE" -czf "$TOP/SOURCES/mako-${VERSION}.tar.gz" .

cat > "$TOP/SPECS/mako.spec" <<EOF
Name:           mako
Version:        ${VERSION}
Release:        1%{?dist}
Summary:        Mako language compiler
License:        MIT
URL:            https://github.com/loreste/mako
Source0:        mako-%{version}.tar.gz

%description
Mako — systems/backend language (.mko → native via C).

%prep
%setup -c -n mako-%{version}

%install
mkdir -p %{buildroot}
cp -a * %{buildroot}/

%files
/usr/bin/mako
/usr/share/mako

%changelog
* $(date '+%a %b %d %Y') Mako contributors - ${VERSION}-1
- Seed package from package-rpm.sh
EOF

if command -v rpmbuild >/dev/null 2>&1; then
  rpmbuild --define "_topdir $ROOT/$TOP" -bb "$TOP/SPECS/mako.spec"
  find "$TOP/RPMS" -name '*.rpm' -exec cp {} dist/ \;
  echo "RPMs copied to dist/"
else
  echo "rpmbuild not found — wrote $TOP/SPECS/mako.spec and source tarball"
fi
