#!/usr/bin/env bash
# Redeploy mako-lang.com edge (Leba TLS proxy) + optional site backend.
#
# Architecture:
#   Internet → Leba :443 (TLS terminate, reverse proxy)
#            → site :8090 (Mako static/dynamic pages, HTTP/1.1)
#
# Host layout (production Contabo VPS):
#   /opt/mako          language tree (git)
#   /opt/leba          Leba load balancer (git)
#   /opt/mako-sip/site site binary + site.mko
#   /etc/systemd/system/leba.service
#   /etc/systemd/system/mako-site.service
#   /opt/leba/mako-lang.conf
#
# Usage (from a machine with root SSH to the host):
#   HOST=root@YOUR_HOST ./scripts/deploy-site.sh
#
# What this does:
#   1. git pull /opt/mako (runtime fixes, e.g. H2 frame split)
#   2. cargo build --release for mako (if needed)
#   3. Rebuild Leba with that mako
#   4. systemctl restart leba (atomic binary swap)
#   5. curl smoke: ALPN, Content-Type, body size
#
# HTTP/2 note:
#   Prefer ALPN http/1.1 at the edge until Leba H2 multi-stream is solid
#   (see runtime/mako_tls.h mako_tls_alpn_cb comment). Browsers then use
#   HTTP/1.1 and avoid ERR_HTTP2_FRAME_SIZE_ERROR from oversized DATA frames.
#   When re-enabling h2 in mako-lang.conf (`protocols http/1.1,h2`), rebuild
#   Leba against a Mako that splits DATA frames ≤16384 (eacbdf6+).

set -euo pipefail

HOST="${HOST:-root@13.140.147.175}"
REMOTE_MAKO="${REMOTE_MAKO:-/opt/mako}"
REMOTE_LEBA="${REMOTE_LEBA:-/opt/leba}"
CONF="${CONF:-/opt/leba/mako-lang.conf}"

echo "==> deploy via $HOST"

ssh "$HOST" bash -s <<REMOTE
set -euo pipefail
export PATH="\$HOME/.cargo/bin:/usr/local/bin:\$PATH"

echo "==> update mako"
cd "$REMOTE_MAKO"
git fetch origin main
git reset --hard origin/main
git log -1 --oneline

echo "==> build mako"
if command -v cargo >/dev/null; then
  cargo build --release
fi
MAKO="$REMOTE_MAKO/target/release/mako"
if [ ! -x "\$MAKO" ]; then
  MAKO=\$(command -v mako)
fi
"\$MAKO" --version

echo "==> build leba"
cd "$REMOTE_LEBA"
export MAKO_RUNTIME="$REMOTE_MAKO/runtime"
# Point Makefile at host mako if present
if [ -x "\$MAKO" ]; then
  "\$MAKO" build --release main.mko -o leba-build
else
  make build
  cp -a leba leba-build
fi
test -x leba-build

echo "==> install + restart leba"
systemctl stop leba
cp -a /usr/local/bin/leba "/usr/local/bin/leba.bak.\$(date +%Y%m%d%H%M%S)" || true
cp -a leba-build /usr/local/bin/leba
chmod +x /usr/local/bin/leba
systemctl start leba
sleep 2
systemctl is-active leba

echo "==> smoke"
echo | openssl s_client -connect 127.0.0.1:443 -servername mako-lang.com -alpn h2,http/1.1 2>/dev/null | grep "ALPN protocol" || true
curl -sS -D- -o /tmp/site-smoke.html --http1.1 -k --max-time 10 \
  https://127.0.0.1/ -H "Host: mako-lang.com" | head -12
wc -c /tmp/site-smoke.html
grep -qi 'content-type: text/html' <(curl -sS -I --http1.1 -k https://127.0.0.1/ -H "Host: mako-lang.com") \
  && echo "Content-Type: text/html OK" || echo "WARN: Content-Type not text/html"
REMOTE

echo "==> public check"
curl -sS -o /dev/null -w "public %{http_code} size=%{size_download} ct=%{content_type} proto=%{http_version}\n" \
  --max-time 10 https://mako-lang.com/ || true

echo "done"
