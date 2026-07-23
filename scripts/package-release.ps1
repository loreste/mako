# Package a self-contained Mako Windows release artifact.
# Usage: ./scripts/package-release.ps1 -ArtifactName mako-x86_64-pc-windows-msvc
param(
    [string]$ArtifactName = "mako-x86_64-pc-windows-msvc"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $Root

$Bin = Join-Path $Root "target\release\mako.exe"
if (-not (Test-Path $Bin)) {
    Write-Host "Building release…"
    cargo build --release
}
if (-not (Test-Path $Bin)) {
    throw "missing $Bin"
}

$Dist = Join-Path $Root "dist"
$Stage = Join-Path $Dist $ArtifactName
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
New-Item -ItemType Directory -Force -Path (Join-Path $Stage "bin") | Out-Null
$Runtime = Join-Path $Stage "share\mako\runtime"
New-Item -ItemType Directory -Force -Path $Runtime | Out-Null
$Std = Join-Path $Stage "share\mako\std"
New-Item -ItemType Directory -Force -Path $Std | Out-Null
$Docs = Join-Path $Stage "share\mako\docs"
New-Item -ItemType Directory -Force -Path $Docs | Out-Null
$Scripts = Join-Path $Stage "scripts"
New-Item -ItemType Directory -Force -Path $Scripts | Out-Null

Copy-Item $Bin (Join-Path $Stage "bin\mako.exe")
Get-ChildItem (Join-Path $Root "runtime\*.h") | ForEach-Object {
    Copy-Item $_.FullName $Runtime
}
foreach ($Source in @("native_runtime.c", "native_bridge.c")) {
    Copy-Item (Join-Path $Root "runtime\$Source") $Runtime
}
$Certs = Join-Path $Root "runtime\certs"
if (Test-Path $Certs) {
    Copy-Item $Certs (Join-Path $Runtime "certs") -Recurse
}
$StdSrc = Join-Path $Root "std"
if (Test-Path $StdSrc) {
    Copy-Item (Join-Path $StdSrc "*") $Std -Recurse
}
$VsSrc = Join-Path $Root "editors\vscode"
if (Test-Path $VsSrc) {
    $Editors = Join-Path $Stage "share\mako\editors"
    New-Item -ItemType Directory -Force -Path $Editors | Out-Null
    Copy-Item $VsSrc (Join-Path $Editors "vscode") -Recurse
}
foreach ($Doc in @("README.md", "CHANGELOG.md", "docs\RELEASE.md", "docs\GUIDE.md", "docs\STATUS.md", "docs\ROADMAP.md", "docs\GENERAL_PURPOSE_PLAN.md", "docs\WASM.md", "docs\ABI.md")) {
    $Src = Join-Path $Root $Doc
    if (Test-Path $Src) {
        $Dst = Join-Path $Docs $Doc
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Dst) | Out-Null
        Copy-Item $Src $Dst
    }
}
Copy-Item (Join-Path $Root "scripts\install.ps1") (Join-Path $Scripts "install.ps1")
Copy-Item (Join-Path $Root "scripts\uninstall.ps1") (Join-Path $Scripts "uninstall.ps1")
Copy-Item (Join-Path $Root "scripts\install.sh") (Join-Path $Scripts "install.sh")
Copy-Item (Join-Path $Root "scripts\uninstall.sh") (Join-Path $Scripts "uninstall.sh")
Copy-Item (Join-Path $Root "scripts\install-release.sh") (Join-Path $Scripts "install-release.sh")

@"
Mako release layout ($ArtifactName)

  bin\mako.exe              — compiler CLI
  share\mako\runtime\       — runtime headers and native support sources
  share\mako\std\           — standard library sources
  share\mako\editors\       — editor integration scaffolds
  share\mako\docs\          — release docs snapshot
  scripts\install.ps1       — install this artifact into Prefix
  scripts\install-release.sh — download, verify, and install release artifacts
  scripts\uninstall.ps1     — remove files installed under Prefix

Install (PowerShell):
  .\scripts\install.ps1 -SkipBuild
  # or set:
  `$env:MAKO_RUNTIME = "`$(Resolve-Path .\share\mako\runtime)"

Requires: Rust (to build from source), clang (LLVM) on PATH to compile .mko.
Docs: docs\RELEASE.md
"@ | Set-Content (Join-Path $Stage "README.txt")

New-Item -ItemType Directory -Force -Path $Dist | Out-Null
$Zip = Join-Path $Dist "$ArtifactName.zip"
if (Test-Path $Zip) { Remove-Item -Force $Zip }
Compress-Archive -Path $Stage -DestinationPath $Zip
Copy-Item (Join-Path $Stage "bin\mako.exe") (Join-Path $Dist "$ArtifactName.exe")
Copy-Item (Join-Path $Root "scripts\install-release.sh") (Join-Path $Dist "install-release.sh")
$HashFile = Join-Path $Dist "$ArtifactName.sha256"
Get-FileHash $Zip -Algorithm SHA256 | ForEach-Object { "$($_.Hash.ToLower())  $(Split-Path -Leaf $Zip)" } | Set-Content $HashFile
Get-FileHash (Join-Path $Dist "$ArtifactName.exe") -Algorithm SHA256 | ForEach-Object { "$($_.Hash.ToLower())  $ArtifactName.exe" } | Add-Content $HashFile
Write-Host "Packed $Zip, $ArtifactName.exe, and $HashFile"
