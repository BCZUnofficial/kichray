# Windows/MSVC twin of run_bloom_tests.py.
#   powershell -ExecutionPolicy Bypass -File tests\r4_deferred\run_bloom_tests.ps1
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$source = [IO.File]::ReadAllText((Join-Path $root 'resources\shaders\r4\bridge_deferred.hlsl'))
function Extract([string]$signature) {
    $start = $source.IndexOf($signature); if ($start -lt 0) { throw "missing $signature" }
    $at = $source.IndexOf('{', $start) + 1; $depth = 1
    while ($depth) { if ($source[$at] -eq '{') { $depth++ } elseif ($source[$at] -eq '}') { $depth-- }; $at++ }
    $source.Substring($start, $at - $start)
}
$helpers = (('float KichBloomAlpha(', 'float4 PSBloomBuild(', 'float4 PSBloomFilter(', 'float4 PSBloomFilterFast(',
    'float4 PSLuminance1(', 'float4 LuminanceBlock(', 'float4 PSLuminance2(', 'float4 PSLuminance3(') | ForEach-Object { Extract $_ }) -join "`n"
$helpers = $helpers.Replace(':SV_Target', '').Replace('[branch]', '').Replace('[unroll]', '')
# Syntax-only adapters: swizzles become C++ helpers; arithmetic stays production code.
$helpers = $helpers.Replace('result.rgb*=light_position.z;', 'result.x*=light_position.z;result.y*=light_position.z;result.z*=light_position.z;')
$helpers = $helpers.Replace(').rgb', ').xyz3()')
$helpers = [regex]::Replace($helpers, '\b(\w+(?:\.\w+)*)\.rgb\b', 'xyz($1)')
$helpers = [regex]::Replace($helpers, '\b(\w+(?:\.\w+)*)\.xy\b', 'xy($1)')
$helpers = [regex]::Replace($helpers, '\.a\b', '.w')
# Scalar .xxxx splat: the C++ float4(float) constructor keeps .x, which the test reads.
$helpers = [regex]::Replace($helpers, '\.r\b', '.x').Replace(').xxxx', ')')
$fixture = [IO.File]::ReadAllText((Join-Path $root 'tests\r4_deferred\bloom_tests.cpp')).Replace('// PRODUCTION_BLOOM', $helpers)
$work = Join-Path ([IO.Path]::GetTempPath()) 'kich-bloom'; New-Item -ItemType Directory -Force $work | Out-Null
[IO.File]::WriteAllText((Join-Path $work 'bloom.cpp'), $fixture)
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars32.bat`" >nul 2>nul && cd /d `"$work`" && cl /nologo /EHsc /O2 /I `"$root\xr_3da`" bloom.cpp /Febloom.exe >nul"
if ($LASTEXITCODE -ne 0) { Write-Output 'FAIL: bloom compile'; exit 1 }
& (Join-Path $work 'bloom.exe'); exit $LASTEXITCODE
