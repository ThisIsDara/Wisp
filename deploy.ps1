# deploy.ps1 - produce a standalone, self-contained deploy folder
param([string]$Config = "release", [string]$QtBinPath = "")
$ErrorActionPreference = "Stop"
if (-not $QtBinPath) {
    $default = "C:\Qt\6.11.1\msvc2022_64\bin"
    if (Test-Path (Join-Path $default "windeployqt.exe")) {
        $QtBinPath = $default
    } elseif (Get-Command windeployqt -ErrorAction SilentlyContinue) {
        $QtBinPath = Split-Path (Get-Command windeployqt).Source
    }
}
$qtBin = $QtBinPath
$windeployqt = Join-Path $qtBin "windeployqt.exe"
$exe     = Join-Path (Join-Path "build" $Config) "wisp.exe"
$outDir  = Join-Path (Join-Path "build" "deploy") "wisp"

if (-not (Test-Path $exe)) { throw "Missing $exe - run build.ps1 -Config release first" }
if (-not (Test-Path $windeployqt)) { throw "windeployqt not found at $windeployqt" }

if (Test-Path $outDir) { Remove-Item -Recurse -Force $outDir }
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
Copy-Item $exe $outDir
Copy-Item "packaging\THIRD-PARTY-NOTICES.txt" $outDir

# --qmldir is MANDATORY: without it windeployqt omits QtQuick modules (PITFALLS #9)
& $windeployqt --release --qmldir qml --no-translations (Join-Path $outDir "wisp.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit $LASTEXITCODE" }

# qt.conf makes the folder relocatable: this Qt 6.11 windeployqt does NOT emit
# one, and without it the app cannot resolve its qml/ import tree on a clean machine.
$qtConf = Join-Path $outDir "qt.conf"
@"
[Paths]
Prefix=.
Libraries=.
Plugins=plugins
Imports=.
QmlImports=qml
"@ | Set-Content -Path $qtConf -Encoding ASCII

Write-Host "Deployed to $outDir"
Write-Host "VC runtime: install VC_redist.x64.exe on target machines (Phase 6) - do not copy dev-machine msvcp/vcruntime DLLs."
