# P0-S5 coverage-preserving detail mips (MSVC).
#   powershell -ExecutionPolicy Bypass -File tests\r4_resources\run_mipgen_tests.ps1
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$work = Join-Path ([IO.Path]::GetTempPath()) 'kich-mipgen'; New-Item -ItemType Directory -Force $work | Out-Null
Copy-Item (Join-Path $root 'tests\r4_resources\mipgen_tests.cpp') (Join-Path $work 'mipgen.cpp') -Force
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars32.bat`" >nul 2>nul && cd /d `"$work`" && cl /nologo /EHsc /O2 /I `"$root\xr_3da`" mipgen.cpp /Femipgen.exe > build.log"
if ($LASTEXITCODE -ne 0) { Get-Content (Join-Path $work 'build.log') | Select-Object -First 30; Write-Output 'FAIL: mipgen compile'; exit 1 }
& (Join-Path $work 'mipgen.exe'); exit $LASTEXITCODE
