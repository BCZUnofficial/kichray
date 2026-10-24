# P0-S3 held sun direction (MSVC).
#   powershell -ExecutionPolicy Bypass -File tests\r4_deferred\run_sun_hold_tests.ps1
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$work = Join-Path ([IO.Path]::GetTempPath()) 'kich-sun-hold'; New-Item -ItemType Directory -Force $work | Out-Null
Copy-Item (Join-Path $root 'tests\r4_deferred\sun_hold_tests.cpp') (Join-Path $work 'hold.cpp') -Force
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars32.bat`" >nul 2>nul && cd /d `"$work`" && cl /nologo /EHsc /O2 /I `"$root\xr_3da`" hold.cpp /Fehold.exe >nul"
if ($LASTEXITCODE -ne 0) { Write-Output 'FAIL: sun hold compile'; exit 1 }
& (Join-Path $work 'hold.exe'); exit $LASTEXITCODE
