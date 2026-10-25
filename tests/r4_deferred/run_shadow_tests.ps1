# Windows/MSVC twin of the C++ part of run_shadow_tests.py.
#   powershell -ExecutionPolicy Bypass -File tests\r4_deferred\run_shadow_tests.ps1
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$source = [IO.File]::ReadAllText((Join-Path $root 'resources\shaders\r4\bridge_deferred.hlsl'))
function Extract([string]$signature) {
    $start = $source.IndexOf($signature); if ($start -lt 0) { throw "missing $signature" }
    $at = $source.IndexOf('{', $start) + 1; $depth = 1
    while ($depth) { if ($source[$at] -eq '{') { $depth++ } elseif ($source[$at] -eq '}') { $depth-- }; $at++ }
    $source.Substring($start, $at - $start)
}
$helpers = (('float3 RelativePosition(', 'float3 WorldPosition(', 'float3 ReceiverPosition(', 'float3 ShadowNumericalOffset(',
    'bool ShadowPlaneStable(', 'float ShadowPlaneErrorSquared(', 'bool ShadowGradient(', 'float ShadowTent(',
    'float3 ShadowDerivative(', 'float PCF(', 'void ShadowWideTexel(', 'float ShadowCrease(', 'float ShadowMapSize(', 'float4 ShadowGather(', 'float ShadowCompare(', 'float PCFWide(', 'void ShadowNormalPlane(') | ForEach-Object { Extract $_ }) -join "`n"
$helpers = $helpers.Replace('float PCF(Texture2D map', 'float PCF(const Texture2D& map').Replace('float PCFWide(Texture2D map', 'float PCFWide(const Texture2D& map').Replace('[unroll]', '').Replace('[branch]', '').Replace('inout float3', 'float3&').Replace('inout float', 'float&')
$helpers = $helpers.Replace('projected.xyz', 'xyz(projected)').Replace('derivative.xyz', 'xyz(derivative)')
$helpers = $helpers.Replace('p.xyz', 'xyz(p)').Replace('camera.xyz', 'xyz(camera)')
$helpers = $helpers.Replace('out float2 gradient', 'float2& gradient').Replace('receiver_inverse[3][3]', 'receiver_inverse.m[15]')
$helpers = $helpers.Replace('return mul(float4(ndc*linear_depth,linear_depth,1),receiver_inverse).xyz;', 'return xyz(mul(float4(ndc*linear_depth,linear_depth,1),receiver_inverse));')
# P0-S6 cascade selection: production ShadowCascadeWeight/SunVisibility.
$cascades = (('float ShadowCascadeWeight(', 'float SunVisibility(') | ForEach-Object { Extract $_ }) -join "`n"
$cascades = $cascades.Replace('[branch]', '').Replace('shadow[2][3][3]', 'shadow[2].m[15]')
$cascades = [regex]::Replace($cascades, '\b(\w+)\.xyz\b', 'xyz($1)')
$helpers = $helpers + "`n" + $cascades
$fixture = [IO.File]::ReadAllText((Join-Path $root 'tests\r4_deferred\shadow_filter_tests.cpp')).Replace('// PRODUCTION_SHADOW_HELPERS', $helpers)
$work = Join-Path ([IO.Path]::GetTempPath()) 'kich-shadow'; New-Item -ItemType Directory -Force $work | Out-Null
[IO.File]::WriteAllText((Join-Path $work 'shadow.cpp'), $fixture)
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars32.bat`" >nul 2>nul && cd /d `"$work`" && cl /nologo /EHsc /O2 /I `"$root\tests\r4_deferred`" shadow.cpp /Feshadow.exe > build.log"
if ($LASTEXITCODE -ne 0) { Get-Content (Join-Path $work 'build.log') | Select-Object -First 30; Write-Output 'FAIL: shadow compile'; exit 1 }
& (Join-Path $work 'shadow.exe'); exit $LASTEXITCODE
