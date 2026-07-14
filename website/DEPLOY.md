# mako-lang.com deploy notes

## Live architecture

```
Browser ──HTTPS:443──► edge TLS proxy ──HTTP:8090──► site binary
                         (TLS terminate)              site backend
                         systemd unit                 systemd: mako-site
```

| Piece | Path |
|-------|------|
| Language tree | `/opt/mako` |
| Edge proxy source / conf | host edge checkout + `mako-lang.conf` |
| Site source | `/opt/mako-sip/site.mko` |
| Site static helpers | `/opt/mako/website/` |
| Certs | `/etc/letsencrypt/live/mako-lang.com/` |

## Symptoms fixed (2026-07-13)

### `net::ERR_HTTP2_FRAME_SIZE_ERROR`

- **Cause:** Edge negotiated ALPN `h2` and sent (or mishandled) large HTML bodies as oversized HTTP/2 DATA frames. Default max frame size is **16384**; homepage is ~**19317** bytes.
- **Backend was fine:** `curl http://127.0.0.1:8090/` already returned `Content-Type: text/html` and the full body.
- **Immediate fix:** Edge build that selects **ALPN `http/1.1` only** (see `mako_tls_alpn_cb` in `runtime/mako_tls.h`). Browsers then use HTTP/1.1 and load the page.
- **Runtime fix (for when H2 is re-enabled):** `eacbdf6` splits TLS H2 DATA into ≤16 KiB frames (`mako_tls_h2_write_data`).
- **Config:** edge `mako-lang.conf` has `protocols http/1.1` under `frontend web`.

### `Content-Type: text/plain` for HTML

- Site backend already emits `text/html; charset=utf-8`.
- After edge redeploy, public responses show `Content-Type: text/html; charset=utf-8`.

## Redeploy

From a machine with root SSH:

```bash
HOST=root@13.140.147.175 ./scripts/deploy-site.sh
```

Or manually on the host (paths match the live edge unit):

```bash
cd /opt/mako && git pull && cargo build --release
# rebuild edge proxy binary against this mako, restart its systemd unit
```

### Smoke

```bash
# ALPN should prefer http/1.1 until H2 is re-enabled safely
echo | openssl s_client -connect mako-lang.com:443 -servername mako-lang.com -alpn h2,http/1.1 2>/dev/null | grep ALPN

curl -sS -D- -o /dev/null --http1.1 https://mako-lang.com/ | head -10
# expect: HTTP/1.1 200, Content-Type: text/html
```

## Rebuild the site backend

```bash
cd /opt/mako-sip
# ensure MAKO points at /opt/mako with current runtime
/opt/mako/target/release/mako build --release site.mko -o site-new
systemctl stop mako-site
cp -a site site-old-$(date +%Y%m%d%H%M%S)
cp -a site-new site
systemctl start mako-site
curl -sS -D- http://127.0.0.1:8090/ | head -10
```

## Re-enabling HTTP/2 (later)

1. Confirm edge H2 multi-stream routing is solid (stream ids for `/docs`, etc.).
2. Rebuild the edge against Mako **≥ eacbdf6** (DATA frame split).
3. Set in `mako-lang.conf`:

   ```
   frontend web
     protocols http/1.1,h2
   ```

4. Smoke with `curl --http2 https://mako-lang.com/` and a browser hard-refresh.
