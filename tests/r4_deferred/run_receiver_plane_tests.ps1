# P0-S9 receiver plane from neighbouring pixels (MSVC; production HLSL helpers).
#   powershell -ExecutionPolicy Bypass -File tests\r4_deferred\run_receiver_plane_tests.ps1
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$source = [IO.File]::ReadAllText((Join-Path $root 'resources\shaders\r4\bridge_deferred.hlsl'))
function Extract([string]$signature) {
    $start = $source.IndexOf($signature); if ($start -lt 0) { throw "missing $signature" }
    $at = $source.IndexOf('{', $start) + 1; $depth = 1
    while ($depth) { if ($source[$at] -eq '{') { $depth++ } elseif ($source[$at] -eq '}') { $depth-- }; $at++ }
    $source.Substring($start, $at - $start)
}
$helpers = (('float3 RelativePosition(', 'float3 ReceiverPosition(', 'float3 LoadReceiverPosition(', 'float ReceiverInverseDepth(',
    'float ShadowSideCurvature(', 'void ShadowDerivativesLoaded(') | ForEach-Object { Extract $_ }) -join "`n"
$helpers = $helpers.Replace('receiver_inverse[3][3]', 'receiver_inverse.m[15]').Replace('inout float3', 'float3&').Replace('int2(screen.xy)', 'int2(int(screen.x),int(screen.y))')
$helpers = $helpers.Replace('return mul(float4(ndc*linear_depth,linear_depth,1),receiver_inverse).xyz;', 'return xyz(mul(float4(ndc*linear_depth,linear_depth,1),receiver_inverse));')
$helpers = [regex]::Replace($helpers, '\b(\w+)\.xyz\b', 'xyz($1)')
$fixture = [IO.File]::ReadAllText((Join-Path $root 'tests\r4_deferred\receiver_plane_tests.cpp')).Replace('// PRODUCTION_RECEIVER_PLANE', $helpers)
$work = Join-Path ([IO.Path]::GetTempPath()) 'kich-receiver-plane'; New-Item -ItemType Directory -Force $work | Out-Null
[IO.File]::WriteAllText((Join-Path $work 'plane.cpp'), $fixture)
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars32.bat`" >nul 2>nul && cd /d `"$work`" && cl /nologo /EHsc /O2 /I `"$root\tests\r4_deferred`" plane.cpp /Feplane.exe > build.log"
if ($LASTEXITCODE -ne 0) { Get-Content (Join-Path $work 'build.log') | Select-Object -First 30; Write-Output 'FAIL: receiver plane compile'; exit 1 }
& (Join-Path $work 'plane.exe'); exit $LASTEXITCODE
