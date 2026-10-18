[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Repo
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Replace-Exact {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Old,
        [Parameter(Mandatory = $true)][string]$New
    )

    $text = [IO.File]::ReadAllText($Path)
    if (-not $text.Contains($Old)) {
        throw "Expected block not found in: $Path"
    }
    $count = ([regex]::Matches($text, [regex]::Escape($Old))).Count
    if ($count -ne 1) {
        throw "Expected block occurs $count times in: $Path"
    }
    [IO.File]::WriteAllText($Path, $text.Replace($Old, $New), [Text.UTF8Encoding]::new($false))
}

$root = (Resolve-Path -LiteralPath $Repo).ProviderPath
$header = Join-Path $root 'xr_3da\xr_efflensflare.h'
$accum  = Join-Path $root 'xr_3da\xrRender_R2\r2_rendertarget_accum_direct.cpp'

foreach ($p in @($header, $accum)) {
    if (-not [IO.File]::Exists($p)) { throw "File not found: $p" }
}

$stamp = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
$backup = Join-Path $root ("_godrays_v3_backups\" + $stamp)
[IO.Directory]::CreateDirectory((Join-Path $backup 'xr_3da\xrRender_R2')) | Out-Null
[IO.File]::Copy($header, (Join-Path $backup 'xr_3da\xr_efflensflare.h'), $true)
[IO.File]::Copy($accum,  (Join-Path $backup 'xr_3da\xrRender_R2\r2_rendertarget_accum_direct.cpp'), $true)

$oldHeader = @'
	int AppendDef(CInifile* pIni, LPCSTR sect);

	void Invalidate()
'@
$newHeader = @'
	int AppendDef(CInifile* pIni, LPCSTR sect);

	// Direct line-of-sight visibility to the current sun/moon source.
	// OnFrame() already evaluates terrain/buildings/dynamic geometry and
	// transparent materials with ObjectSpace.RayQuery and smooths the result.
	float GetSourceVisibility() const
	{
		return fBlend;
	}

	void Invalidate()
'@
Replace-Exact -Path $header -Old $oldHeader -New $newHeader

$oldIncludes = @'
#include "stdafx.h"
#include "..\igame_persistent.h"
#include "..\environment.h"
'@
$newIncludes = @'
#include "stdafx.h"
#include "..\igame_persistent.h"
#include "..\environment.h"
#include "..\xr_efflensflare.h"
'@
Replace-Exact -Path $accum -Old $oldIncludes -New $newIncludes

$oldParams = @'
    RCache.set_c("Ldynamic_dir", sun_dir.x, sun_dir.y, sun_dir.z, 0.f);
    RCache.set_c("Ldynamic_color", sun_color.x, sun_color.y, sun_color.z, 0.f);
    RCache.set_c("kich_sun_shafts_params", samples, ps_r2_sun_shafts_intensity, zNear, zFar);
'@
$newParams = @'
    // KICH_SHAFTS_OCCLUSION_V3: the volumetric pass previously knew only the
    // shadow map.  The visible sun/moon disc already has an independent CPU
    // line-of-sight test (terrain, buildings, dynamic objects and material
    // transparency) in CLensFlare::OnFrame().  Use that same visibility as a
    // global gate for shafts, so an occluded celestial source cannot leave a
    // strong residual beam through a ridge/building.
    float sourceVisibility = 1.f;
    if (g_pGamePersistent)
    {
        CEnvironment& env = g_pGamePersistent->Environment();
        if (env.eff_LensFlare)
            sourceVisibility = env.eff_LensFlare->GetSourceVisibility();
    }
    clamp(sourceVisibility, 0.f, 1.f);

    RCache.set_c("Ldynamic_dir", sun_dir.x, sun_dir.y, sun_dir.z, 0.f);
    RCache.set_c("Ldynamic_color", sun_color.x, sun_color.y, sun_color.z, 0.f);
    RCache.set_c("kich_sun_shafts_params", samples,
                 ps_r2_sun_shafts_intensity * sourceVisibility, zNear, zFar);
'@
Replace-Exact -Path $accum -Old $oldParams -New $newParams

Write-Host "God-rays occlusion v3 source patch applied."
Write-Host "Backup: $backup"
Write-Host "Rebuild the R2 renderer (Release / Win32) before testing."
