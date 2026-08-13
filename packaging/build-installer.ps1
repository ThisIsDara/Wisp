# build-installer.ps1 - produce wisp-setup.exe (per-user NSIS installer, D-13/D-14)
# Pipeline: fresh release build (if stale) -> deploy.ps1 (windeployqt output) ->
#           VC_redist download -> makensis
param([string]$Config = "release")
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $repoRoot

# --- Step 0: ensure the release binary is current --------------------------
# Never package a stale wisp.exe: if missing or older than any source file,
# rebuild via build.ps1 (same vcvars64 pattern as dev builds).
$releaseExe = Join-Path (Join-Path "build" $Config) "wisp.exe"
$stale = -not (Test-Path $releaseExe)
if (-not $stale) {
    $exeTime = (Get-Item $releaseExe).LastWriteTime
    $newer = Get-ChildItem "src", "qml" -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -in @(".cpp", ".h", ".hpp", ".qml", ".qrc", ".rc", ".ui") -and $_.LastWriteTime -gt $exeTime }
    if ($newer) { $stale = $true }
}
if ($stale) {
    Write-Host "Step 0/4: release binary missing or stale - rebuilding (build.ps1 -Config $Config)..."
    & (Join-Path $PSScriptRoot "..\build.ps1") -Config $Config
    if ($LASTEXITCODE -ne 0) { throw "release build failed with exit $LASTEXITCODE" }
}

# --- Step 1: deploy output (windeployqt) ------------------------------------
Write-Host "Step 1/4: producing deploy output (build/deploy/wisp)..."
& (Join-Path $PSScriptRoot "..\deploy.ps1") -Config $Config
if ($LASTEXITCODE -ne 0) { throw "deploy.ps1 failed with exit $LASTEXITCODE" }

# --- Step 2: download VC_redist (official aka.ms URL only - T-06-03) --------
$deployDir = Join-Path (Join-Path "build" "deploy") "wisp"
$redist = Join-Path $deployDir "vc_redist.x64.exe"
if (-not (Test-Path $redist) -or (Get-Item $redist).Length -lt 1MB) {
    Write-Host "Step 2/4: downloading VC_redist.x64.exe from aka.ms (official)..."
    Invoke-WebRequest "https://aka.ms/vs/17/release/vc_redist.x64.exe" -OutFile $redist
}
$redistLen = (Get-Item $redist).Length
if ($redistLen -lt 1MB) { throw "VC_redist download suspiciously small ($redistLen bytes) - aborting (T-06-03)" }
Write-Host "VC_redist.x64.exe: $redistLen bytes"

# --- Step 3: locate makensis and build the installer --------------------------
Write-Host "Step 3/4: building installer with makensis..."
$makensis = Get-Command makensis -ErrorAction SilentlyContinue
if (-not $makensis) {
    $candidates = @(
        (Join-Path $env:LOCALAPPDATA "Programs\NSIS\makensis.exe"),
        (Join-Path $env:ProgramFiles "NSIS\makensis.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "NSIS\makensis.exe"),
        (Join-Path $repoRoot "build\tools\NSIS\makensis.exe")   # portable NSIS fallback
    )
    $chocoLibs = Get-ChildItem "C:\ProgramData\chocolatey\lib" -Directory -Filter "nsis*" -ErrorAction SilentlyContinue
    foreach ($d in $chocoLibs) {
        $found = Get-ChildItem $d.FullName -Recurse -Filter "makensis.exe" -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($found) { $candidates += $found.FullName }
    }
    foreach ($c in $candidates) {
        if ($c -and (Test-Path $c)) { $makensis = Get-Item $c; break }
    }
}
if (-not $makensis) { throw "makensis not found - install NSIS (winget install NSIS.NSIS) or place portable NSIS at build\tools\NSIS" }
$makensisPath = if ($makensis -is [System.Management.Automation.CommandInfo]) { $makensis.Source } else { $makensis.FullName }
Write-Host "makensis: $makensisPath"
& $makensisPath (Join-Path $PSScriptRoot "installer.nsi")
if ($LASTEXITCODE -ne 0) { throw "makensis failed with exit $LASTEXITCODE" }

# --- done --------------------------------------------------------------------
$installer = Join-Path (Join-Path "build" "deploy") "wisp-setup.exe"
if (-not (Test-Path $installer)) { throw "installer was not produced: $installer" }
$size = (Get-Item $installer).Length
Write-Host "Installer: $installer ($size bytes)"
Write-Host "build-installer.ps1: PASS"
exit 0
