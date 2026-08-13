# run.ps1 - launch wisp.exe with the Qt 6.11.1 bin on PATH (debug DLLs live there)
param(
    [string]$Config = "dev"
)

$ErrorActionPreference = "Stop"

$exe = Join-Path (Join-Path "build" $Config) "wisp.exe"
if (-not (Test-Path $exe)) {
    throw "Missing $exe - run build.ps1 first"
}

$env:Path = "C:\Qt\6.11.1\msvc2022_64\bin;$env:Path"
& $exe
