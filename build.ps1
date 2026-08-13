# build.ps1 - CLI-first build driver (D-07): cmake --preset + Ninja
param(
    [string]$Config = "dev",
    [string]$QtBinPath = ""    # Qt bin dir; empty = default C:\Qt\6.11.1\msvc2022_64\bin
                                # or whatever qmake/windeployqt is on PATH (CI)
)

$ErrorActionPreference = "Stop"

if (-not $QtBinPath) {
    $default = "C:\Qt\6.11.1\msvc2022_64\bin"
    if (Test-Path (Join-Path $default "qmake.exe")) {
        $QtBinPath = $default
    } elseif (Get-Command qmake -ErrorAction SilentlyContinue) {
        $QtBinPath = Split-Path (Get-Command qmake).Source
    }
}
if (-not $QtBinPath -or -not (Test-Path (Join-Path $QtBinPath "qmake.exe"))) {
    throw "Qt bin dir not found - pass -QtBinPath or install Qt 6.11.1 at C:\Qt\6.11.1\msvc2022_64"
}
$env:Path = "$QtBinPath;$env:Path"

$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
        if ($vsRoot) { $vcvars = Join-Path $vsRoot "VC\Auxiliary\Build\vcvars64.bat" }
    }
}
if (-not (Test-Path $vcvars)) {
    throw "vcvars64.bat not found - install VS 2022 'Desktop development with C++'"
}

if ($Config -ne "dev" -and $Config -ne "release") {
    throw "Config must be 'dev' or 'release' (got '$Config')"
}

# vcvars64.bat is a cmd script; cmake must run with the MSVC environment loaded
cmd /c "`"$vcvars`" >nul 2>&1 && cmake --preset $Config && cmake --build --preset $Config"
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$exe = Join-Path (Join-Path "build" $Config) "wisp.exe"
if (-not (Test-Path $exe)) {
    throw "Build finished but $exe was not produced"
}

Write-Host "Built: $exe"
