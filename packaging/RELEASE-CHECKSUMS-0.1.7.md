# Release checksums v0.1.7

Full multi-OS Release CI succeeded. First hash is the install archive (`.tar.gz` / `.zip`).

| Asset | SHA-256 |
|-------|---------|
| source `v0.1.7.tar.gz` | `b602639b1710c3d6154bfabc0198446a3561e9d4f41057d2d964b9d7333d940c` |
| `mako-x86_64-unknown-linux-gnu.tar.gz` | `815e285c855924ea6c658511bbee2f9daf91a53650b9d229eded9c3d5308e0f5` |
| `mako-aarch64-apple-darwin.tar.gz` | `52a994a1ae0247021292db62f126d030b9fa648a56e5a88079fb2e20ea9e1721` |
| `mako-x86_64-pc-windows-msvc.zip` | `085073dd4935934fee8e91c6084932ba113bb011fa5df5d7ad1f582b9aa34864` |

Winget `InstallerSha256` (Windows zip, uppercase):  
`085073DD4935934FEE8E91C6084932BA113BB011FA5DF5D7AD1F582B9AA34864`

Install (after assets published):

```bash
curl -fsSL https://github.com/loreste/mako/releases/download/v0.1.7/install-release.sh \
  | bash -s -- --version v0.1.7 --yes
```

Release: https://github.com/loreste/mako/releases/tag/v0.1.7
