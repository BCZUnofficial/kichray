# P0-S11 shadow face admission history (MSVC).
#   powershell -ExecutionPolicy Bypass -File tests\r4_deferred\run_face_admission_tests.ps1
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$work = Join-Path ([IO.Path]::GetTempPath()) 'kich-face-admission'; New-Item -ItemType Directory -Force $work | Out-Null
Copy-Item (Join-Path $root 'tests\r4_deferred\face_admission_tests.cpp') (Join-Path $work 'faces.cpp') -Force
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars32.bat`" >nul 2>nul && cd /d `"$work`" && cl /nologo /EHsc /O2 /W3 /I `"$root\xr_3da`" faces.cpp /Fefaces.exe > build.log"
if ($LASTEXITCODE -ne 0) { Get-Content (Join-Path $work 'build.log') | Select-Object -First 30; Write-Output 'FAIL: face admission compile'; exit 1 }
& (Join-Path $work 'faces.exe'); exit $LASTEXITCODE
