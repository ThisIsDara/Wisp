# verify-lgpl.ps1 - LGPL compliance evidence (D-15)
#   Check 1: dumpbin /DEPENDENTS on the deployed wisp.exe -> Qt6*.dll imports
#            (Qt is dynamically linked, never statically embedded)
#   Check 2: relink test - compile an independent binary against Qt's import
#            lib, run it with PATH = deploy folder ONLY (Pitfall 5: the loaded
#            Qt6Core.dll must be the DEPLOYED one, not the dev Qt)
# Exit 0 only if both checks pass.
$ErrorActionPreference = "Stop"

# --- resolve toolchain ---------------------------------------------------
$dumpbin = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC" -Directory |
    ForEach-Object { Join-Path $_.FullName "bin\Hostx64\x64\dumpbin.exe" } |
    Where-Object { Test-Path $_ } |
    Select-Object -First 1
if (-not $dumpbin) { throw "dumpbin.exe not found - is the VS2022 'Desktop development with C++' workload installed?" }

$deploy = "build\deploy\wisp"
$exe = Join-Path $deploy "wisp.exe"
if (-not (Test-Path $exe)) { throw "Missing $exe - run .\packaging\build-installer.ps1 first" }

$qtRoot = "C:\Qt\6.11.1\msvc2022_64"
if (-not (Test-Path (Join-Path $qtRoot "lib\Qt6Core.lib"))) { throw "Qt import lib not found under $qtRoot" }

$failed = $false

# --- Check 1: dynamic linkage evidence ------------------------------------
Write-Host "== Check 1/2: dumpbin /DEPENDENTS - Qt6 imports on wisp.exe =="
$deps = & $dumpbin /DEPENDENTS $exe
if ($LASTEXITCODE -ne 0) { throw "dumpbin failed with exit $LASTEXITCODE" }
foreach ($dll in @("Qt6Core.dll", "Qt6Gui.dll", "Qt6Qml.dll", "Qt6Quick.dll")) {
    if ($deps -match [regex]::Escape($dll)) { Write-Host "  PASS: $dll imported" }
    else { Write-Host "  FAIL: $dll NOT imported"; $failed = $true }
}

# --- Check 2: relink test --------------------------------------------------
Write-Host "== Check 2/2: relink test - binary runs against DEPLOYED Qt6Core.dll =="
$src = "packaging\relink-test\main.cpp"
$outDir = "build\relink-test"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$bin = Join-Path $outDir "relink-test.exe"

$cl = Join-Path (Split-Path $dumpbin -Parent) "cl.exe"
if (-not (Test-Path $cl)) { throw "cl.exe not found at $cl" }
$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found at $vcvars" }

$include = Join-Path $qtRoot "include"
$includeQtCore = Join-Path $include "QtCore"
$lib = Join-Path $qtRoot "lib\Qt6Core.lib"

# Load the MSVC environment (INCLUDE/LIB/PATH) from vcvars64.bat into this
# session by capturing `set` output - avoids cmd.exe quoting mangling.
$vcvarsOut = cmd /c "`"$vcvars`" >nul 2>&1 && set"
foreach ($line in $vcvarsOut) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
    }
}
& $cl /nologo /EHsc /std:c++17 /Zc:__cplusplus /permissive- $src /I $include /I $includeQtCore /Fo:"$outDir\" /Fe:"$bin" /link $lib
if ($LASTEXITCODE -ne 0) { throw "cl compile failed with exit $LASTEXITCODE" }

# run with PATH = deploy folder ONLY (Pitfall 5: never resolve the dev Qt)
$oldPath = $env:PATH
try {
    $env:PATH = (Resolve-Path $deploy).Path
    $relinkOut = & $bin 2>&1
    $relinkExit = $LASTEXITCODE
}
finally { $env:PATH = $oldPath }

if ($relinkExit -ne 0) { Write-Host "  FAIL: relink binary exited $relinkExit"; $failed = $true }
elseif (($relinkOut -join "`n") -match "RELINK OK") {
    Write-Host "  PASS: relink binary ran against deployed Qt6Core.dll"
    $relinkOut | ForEach-Object { Write-Host "    $_" }
}
else {
    Write-Host "  FAIL: no 'RELINK OK' in relink output"
    $relinkOut | ForEach-Object { Write-Host "    $_" }
    $failed = $true
}

if ($failed) { Write-Host "LGPL verification: FAIL"; exit 1 }
Write-Host "LGPL verification: PASS"
exit 0
