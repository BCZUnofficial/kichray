# Windows/MSVC twin of run_sun_tests.py.
#   powershell -ExecutionPolicy Bypass -File tests\r4_deferred\run_sun_tests.ps1
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$source = [IO.File]::ReadAllText((Join-Path $root 'resources\shaders\r4\bridge_deferred.hlsl'))
function Extract([string]$signature) {
    $start = $source.IndexOf($signature); if ($start -lt 0) { throw "missing $signature" }
    $at = $source.IndexOf('{', $start) + 1; $depth = 1
    while ($depth) { if ($source[$at] -eq '{') { $depth++ } elseif ($source[$at] -eq '}') { $depth-- }; $at++ }
    $source.Substring($start, $at - $start)
}
$helpers = (('float PackMaterial(', 'float MaterialGloss(', 'float MaterialClass(', 'float2 MaterialLutSlice(', 'float2 MaterialResponse(',
    'float SpecularIntensity(', 'float FogTransmittance(', 'float3 ForwardFog(', 'float SunMask(', 'float SunDirect(', 'float4 DirectLight(',
    'float3 ForwardIndirect(', 'void ShadowNormalPlane(', 'float SunShadowing(', 'float4 SunLighting(', 'float SunFarFade(', 'float4 PSSun(', 'float4 PSForwardSun(') | ForEach-Object { Extract $_ }) -join "`n"
# Syntax-only adapters: execute the production branches and numerical expressions.
$helpers = $helpers.Replace(':SV_Target', '').Replace(':SV_IsFrontFace', '').Replace('[branch]', '').Replace('inout float3', 'float3&')
$helpers = $helpers.Replace('visibility.xxx', 'splat3(visibility)')
$helpers = [regex]::Replace($helpers, '\b(\w+(?:\.\w+)*)\.(?:rgb|xyz)\b', 'xyz($1)')
$helpers = [regex]::Replace($helpers, '\b(\w+(?:\.\w+)*)\.xy\b', 'xy($1)')
$helpers = [regex]::Replace($helpers, '\b(\w+(?:\.\w+)*)\.aaa\b', 'splat3($1.w)')
$helpers = ([regex]::Replace($helpers, '//[^\n]*', '')).Replace(';', ";`n")
$fixture = [IO.File]::ReadAllText((Join-Path $root 'tests\r4_deferred\sun_lighting_tests.cpp')).Replace('// PRODUCTION_SUN_HELPERS', $helpers)
$work = Join-Path ([IO.Path]::GetTempPath()) 'kich-sun'; New-Item -ItemType Directory -Force $work | Out-Null
[IO.File]::WriteAllText((Join-Path $work 'sun.cpp'), $fixture)
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars32.bat`" >nul 2>nul && cd /d `"$work`" && cl /nologo /EHsc /O2 /I `"$root\xr_3da`" sun.cpp /Fesun.exe > build.log"
if ($LASTEXITCODE -ne 0) { Get-Content (Join-Path $work 'build.log') | Select-Object -First 30; Write-Output 'FAIL: sun compile'; exit 1 }
& (Join-Path $work 'sun.exe'); exit $LASTEXITCODE
