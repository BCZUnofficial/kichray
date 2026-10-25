# P0-S8 static lamp shadow cache policy (MSVC).
#   powershell -ExecutionPolicy Bypass -File tests\r4_deferred\run_local_shadow_cache_tests.ps1
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$work = Join-Path ([IO.Path]::GetTempPath()) 'kich-local-cache'; New-Item -ItemType Directory -Force $work | Out-Null
Copy-Item (Join-Path $root 'tests\r4_deferred\local_shadow_cache_tests.cpp') (Join-Path $work 'cache.cpp') -Force
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars32.bat`" >nul 2>nul && cd /d `"$work`" && cl /nologo /EHsc /O2 /W3 /I `"$root\xr_3da`" cache.cpp /Fecache.exe > build.log"
if ($LASTEXITCODE -ne 0) { Get-Content (Join-Path $work 'build.log') | Select-Object -First 30; Write-Output 'FAIL: local shadow cache compile'; exit 1 }
& (Join-Path $work 'cache.exe'); exit $LASTEXITCODE
