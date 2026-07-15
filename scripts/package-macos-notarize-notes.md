# macOS pkg / notarization seed

## Local install (current)

```bash
make install
# or
./scripts/install.sh
```

## Productize later

1. Build universal or arm64/x86_64 binaries (`mako build --target …` / lipo).
2. Package:
   ```bash
   pkgbuild --root stage --identifier dev.mako.cli --version 0.1.2 mako.pkg
   ```
3. Sign with Developer ID Installer certificate.
4. Notarize:
   ```bash
   xcrun notarytool submit mako.pkg --apple-id … --team-id … --password …
   xcrun stapler staple mako.pkg
   ```

Requires Apple Developer Program credentials — not automated in open CI without secrets.

Homebrew formula seed: `Formula/mako.rb` (build-from-source).
