# Windows/MSVC twin of run_material_lut_tests.py for machines without Python.
#   powershell -ExecutionPolicy Bypass -File tests\r4_materials\run_material_lut_tests.ps1
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$source = [IO.File]::ReadAllText((Join-Path $root 'resources\shaders\r4\bridge_deferred.hlsl'))
function Extract([string]$signature) {
    $start = $source.IndexOf($signature); if ($start -lt 0) { throw "missing $signature" }
    $at = $source.IndexOf('{', $start) + 1; $depth = 1
    while ($depth) { if ($source[$at] -eq '{') { $depth++ } elseif ($source[$at] -eq '}') { $depth-- }; $at++ }
    $source.Substring($start, $at - $start)
}
$helpers = ((Extract 'float2 MaterialLutSlice(') + "`n" + (Extract 'float2 MaterialResponse(')).Replace('[branch]', '')
$fixture = [IO.File]::ReadAllText((Join-Path $root 'tests\r4_materials\material_lut_sampling_tests.cpp')).Replace('// PRODUCTION_MATERIAL_LUT', $helpers)
$work = Join-Path ([IO.Path]::GetTempPath()) 'kich-p0-l3'; New-Item -ItemType Directory -Force $work | Out-Null
[IO.File]::WriteAllText((Join-Path $work 'sampling.cpp'), $fixture)
Copy-Item (Join-Path $root 'tests\r4_materials\material_lut_tests.cpp') (Join-Path $work 'generation.cpp') -Force
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -property installationPath
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars32.bat'
$failed = 0
foreach ($name in 'generation', 'sampling') {
    cmd /c "`"$vcvars`" >nul 2>nul && cd /d `"$work`" && cl /nologo /EHsc /O2 /I `"$root\xr_3da`" $name.cpp /Fe$name.exe >nul"
    if ($LASTEXITCODE -ne 0) { Write-Output "FAIL: $name compile"; $failed++; continue }
    & (Join-Path $work "$name.exe")
    if ($LASTEXITCODE -ne 0) { $failed++ }
}
exit $failed
