# Install mako binary + runtime headers on Windows.
# Usage:
#   .\scripts\install.ps1
#   .\scripts\install.ps1 -Prefix "$env:LOCALAPPDATA\mako" -SkipBuild
param(
    [string]$Prefix = $(Join-Path $env:USERPROFILE ".local"),
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $Root

$BinDir = Join-Path $Prefix "bin"
$RuntimeDst = Join-Path $Prefix "share\mako\runtime"
$StdDst = Join-Path $Prefix "share\mako\std"
$EditorsDst = Join-Path $Prefix "share\mako\editors"

Write-Host "mako install"
Write-Host "  prefix:  $Prefix"
Write-Host "  bin:     $BinDir"
Write-Host "  runtime: $RuntimeDst"
Write-Host "  std:     $StdDst"

if (-not $SkipBuild) {
    Write-Host "Building release binary…"
    cargo build --release
}

$Bin = Join-Path $Root "target\release\mako.exe"
if (-not (Test-Path $Bin)) {
    $PackagedBin = Join-Path $Root "bin\mako.exe"
    if (Test-Path $PackagedBin) { $Bin = $PackagedBin }
}
if (-not (Test-Path $Bin)) {
    throw "missing $Bin — run cargo build --release first"
}
$RuntimeSrc = Join-Path $Root "runtime"
if (-not (Test-Path (Join-Path $RuntimeSrc "mako_rt.h"))) {
    $PackagedRuntime = Join-Path $Root "share\mako\runtime"
    if (Test-Path (Join-Path $PackagedRuntime "mako_rt.h")) { $RuntimeSrc = $PackagedRuntime }
}
$StdSrc = Join-Path $Root "std"
if (-not (Test-Path $StdSrc)) {
    $PackagedStd = Join-Path $Root "share\mako\std"
    if (Test-Path $PackagedStd) { $StdSrc = $PackagedStd }
}
$VsSrc = Join-Path $Root "editors\vscode"
if (-not (Test-Path $VsSrc)) {
    $PackagedVs = Join-Path $Root "share\mako\editors\vscode"
    if (Test-Path $PackagedVs) { $VsSrc = $PackagedVs }
}
if (-not (Test-Path (Join-Path $RuntimeSrc "mako_rt.h"))) {
    throw "missing runtime\mako_rt.h (looked under checkout and release artifact layout)"
}

New-Item -ItemType Directory -Force -Path $BinDir | Out-Null
New-Item -ItemType Directory -Force -Path $RuntimeDst | Out-Null
New-Item -ItemType Directory -Force -Path $StdDst | Out-Null
Copy-Item $Bin (Join-Path $BinDir "mako.exe") -Force
Get-ChildItem (Join-Path $RuntimeSrc "*.h") | ForEach-Object {
    Copy-Item $_.FullName $RuntimeDst -Force
}
$Certs = Join-Path $RuntimeSrc "certs"
if (Test-Path $Certs) {
    $CertDst = Join-Path $RuntimeDst "certs"
    New-Item -ItemType Directory -Force -Path $CertDst | Out-Null
    Copy-Item (Join-Path $Certs "*") $CertDst -Recurse -Force
}
if (Test-Path $StdSrc) {
    if (Test-Path $StdDst) { Remove-Item $StdDst -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $StdDst | Out-Null
    Copy-Item (Join-Path $StdSrc "*") $StdDst -Recurse -Force
}
if (Test-Path $VsSrc) {
    New-Item -ItemType Directory -Force -Path $EditorsDst | Out-Null
    $VsDst = Join-Path $EditorsDst "vscode"
    if (Test-Path $VsDst) { Remove-Item $VsDst -Recurse -Force }
    Copy-Item $VsSrc $VsDst -Recurse -Force
}

$ShareDir = Join-Path $Prefix "share\mako"
$VerLine = & (Join-Path $BinDir "mako.exe") version 2>$null
if (-not $VerLine) { $VerLine = "unknown" }
$HostId = "$([System.Environment]::OSVersion.Platform)-$([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture)"
$Ts = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
$Manifest = @{
    schema = "mako.install.v1"
    version = "$VerLine"
    prefix = "$Prefix"
    host = "$HostId"
    installedAt = "$Ts"
    runtime = "$RuntimeDst"
    std = "$StdDst"
} | ConvertTo-Json
$ManifestPath = Join-Path $ShareDir "install-manifest.json"
New-Item -ItemType Directory -Force -Path $ShareDir | Out-Null
Set-Content -Path $ManifestPath -Value $Manifest -Encoding UTF8

Write-Host "Installed $(Join-Path $BinDir 'mako.exe')"
Write-Host "Installed runtime → $RuntimeDst"
Write-Host "Installed stdlib  → $StdDst"
if (Test-Path (Join-Path $EditorsDst "vscode")) {
    Write-Host "Installed VS Code scaffold → $(Join-Path $EditorsDst 'vscode')"
}
Write-Host "Manifest: $ManifestPath"
Write-Host "Add to PATH: $BinDir"
Write-Host "Optional: `$env:MAKO_RUNTIME = '$RuntimeDst'"
Write-Host "Requires clang (LLVM) on PATH to compile .mko programs."
Write-Host "Verify: $(Join-Path $BinDir 'mako.exe') doctor"
Write-Host "Docs: docs\RELEASE.md"
