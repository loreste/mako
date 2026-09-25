# =============================================================================
# Mako Windows One-Shot Installer — Prebuilt Binary
# =============================================================================
#
#   irm https://github.com/loreste/mako/releases/latest/download/install-windows.ps1 | iex
#
# What you get in PREFIX (default %LOCALAPPDATA%\mako):
#   bin\mako.exe              — compiler CLI
#   share\mako\runtime\       — C runtime headers
#   share\mako\std\           — standard library
# =============================================================================
param(
    [string]$Version = "latest",
    [string]$Prefix = $(Join-Path $env:LOCALAPPDATA "mako"),
    [switch]$NoPath,
    [switch]$NoDeps,
    [switch]$NoDoctor,
    [switch]$Yes
)

$ErrorActionPreference = "Stop"
$Artifact = "mako-x86_64-pc-windows-msvc"

Write-Host "Mako installer for Windows" -ForegroundColor Cyan
Write-Host "  prefix: $Prefix"

# --- Resolve download URL ---
if ($Version -eq "latest") {
    $ReleaseUrl = "https://api.github.com/repos/loreste/mako/releases/latest"
    Write-Host "Fetching latest release..."
    try {
        $Release = Invoke-RestMethod -Uri $ReleaseUrl -Headers @{ "User-Agent" = "mako-installer" }
        $Tag = $Release.tag_name
    } catch {
        Write-Error "Failed to fetch latest release: $_"
        exit 1
    }
} else {
    $Tag = $Version
    if (-not $Tag.StartsWith("v")) { $Tag = "v$Tag" }
}

$ZipUrl = "https://github.com/loreste/mako/releases/download/$Tag/$Artifact.zip"
Write-Host "Downloading $Tag ($Artifact)..."

# --- Download and extract ---
$TmpDir = Join-Path $env:TEMP "mako-install-$([guid]::NewGuid().ToString('N').Substring(0,8))"
$ZipPath = Join-Path $TmpDir "$Artifact.zip"
New-Item -ItemType Directory -Force -Path $TmpDir | Out-Null

try {
    Invoke-WebRequest -Uri $ZipUrl -OutFile $ZipPath -UseBasicParsing
} catch {
    Write-Error "Download failed. Check that release $Tag exists at:`n  $ZipUrl"
    exit 1
}

$ExtractDir = Join-Path $TmpDir "extracted"
Expand-Archive -Path $ZipPath -DestinationPath $ExtractDir -Force

# Find the extracted artifact directory
$ArtifactDir = Get-ChildItem -Path $ExtractDir -Directory | Select-Object -First 1
if (-not $ArtifactDir) {
    # Files might be at root of zip
    $ArtifactDir = Get-Item $ExtractDir
}

# --- Install ---
$BinDir = Join-Path $Prefix "bin"
$RuntimeDst = Join-Path $Prefix "share\mako\runtime"
$StdDst = Join-Path $Prefix "share\mako\std"

New-Item -ItemType Directory -Force -Path $BinDir | Out-Null
New-Item -ItemType Directory -Force -Path $RuntimeDst | Out-Null

# Find mako.exe in extracted content
$MakoExe = Get-ChildItem -Path $ExtractDir -Recurse -Filter "mako.exe" | Select-Object -First 1
if (-not $MakoExe) {
    Write-Error "mako.exe not found in downloaded archive"
    exit 1
}
Copy-Item $MakoExe.FullName (Join-Path $BinDir "mako.exe") -Force
Write-Host "  installed: $(Join-Path $BinDir 'mako.exe')" -ForegroundColor Green

# Copy runtime headers
$RuntimeSrc = Join-Path $MakoExe.Directory.FullName "share\mako\runtime"
if (-not (Test-Path $RuntimeSrc)) {
    $RuntimeSrc = Get-ChildItem -Path $ExtractDir -Recurse -Directory -Filter "runtime" |
        Where-Object { Test-Path (Join-Path $_.FullName "mako_rt.h") } |
        Select-Object -First 1
    if ($RuntimeSrc) { $RuntimeSrc = $RuntimeSrc.FullName }
}
if ($RuntimeSrc -and (Test-Path $RuntimeSrc)) {
    if (Test-Path $RuntimeDst) { Remove-Item $RuntimeDst -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $RuntimeDst | Out-Null
    Copy-Item (Join-Path $RuntimeSrc "*") $RuntimeDst -Recurse -Force
    Write-Host "  runtime:   $RuntimeDst" -ForegroundColor Green
}

# Copy std library
$StdSrc = Join-Path $MakoExe.Directory.FullName "share\mako\std"
if (-not (Test-Path $StdSrc)) {
    $StdSrc = Get-ChildItem -Path $ExtractDir -Recurse -Directory -Filter "std" |
        Where-Object { Get-ChildItem $_.FullName -Filter "*.mko" -ErrorAction SilentlyContinue } |
        Select-Object -First 1
    if ($StdSrc) { $StdSrc = $StdSrc.FullName }
}
if ($StdSrc -and (Test-Path $StdSrc)) {
    if (Test-Path $StdDst) { Remove-Item $StdDst -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $StdDst | Out-Null
    Copy-Item (Join-Path $StdSrc "*") $StdDst -Recurse -Force
    Write-Host "  stdlib:    $StdDst" -ForegroundColor Green
}

# --- Add to PATH ---
if (-not $NoPath) {
    $UserPath = [Environment]::GetEnvironmentVariable("PATH", "User")
    if ($UserPath -notlike "*$BinDir*") {
        [Environment]::SetEnvironmentVariable("PATH", "$BinDir;$UserPath", "User")
        $env:PATH = "$BinDir;$env:PATH"
        Write-Host "  Added $BinDir to user PATH" -ForegroundColor Green
    }
}

# --- Set MAKO_RUNTIME env var ---
[Environment]::SetEnvironmentVariable("MAKO_RUNTIME", $RuntimeDst, "User")
$env:MAKO_RUNTIME = $RuntimeDst

# --- Set MAKO_STD env var ---
[Environment]::SetEnvironmentVariable("MAKO_STD", $StdDst, "User")
$env:MAKO_STD = $StdDst

# --- Cleanup ---
Remove-Item $TmpDir -Recurse -Force -ErrorAction SilentlyContinue

# --- Verify ---
Write-Host ""
Write-Host "Installation complete!" -ForegroundColor Cyan
try {
    $ver = & (Join-Path $BinDir "mako.exe") --version 2>&1
    Write-Host "  $ver"
} catch {
    Write-Host "  (verify manually: mako --version)" -ForegroundColor Yellow
}

if (-not $NoDoctor) {
    Write-Host ""
    Write-Host "Running mako doctor..." -ForegroundColor Cyan
    try { & (Join-Path $BinDir "mako.exe") doctor } catch {}
}

Write-Host ""
Write-Host "Quick start:" -ForegroundColor Cyan
Write-Host "  mako init myapp --name myapp"
Write-Host "  cd myapp && mako run main.mko"
Write-Host ""
Write-Host "Note: clang (LLVM) is required to compile .mko programs."
Write-Host "  Install: winget install LLVM.LLVM  (or choco install llvm)"
