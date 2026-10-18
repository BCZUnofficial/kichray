# Experiment 1: Clear Sky style sun shafts for kichray (DX9 R2).
# Windows PowerShell 5.1 / PowerShell 7; Python is not required.
# Validates every source anchor before modifying files, makes backups, and rolls back on write errors.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [Alias('Path', 'Repository')]
    [string]$Repo,
    [switch]$Check
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Replace-One {
    param(
        [Parameter(Mandatory = $true)][string]$Content,
        [Parameter(Mandatory = $true)][string]$Anchor,
        [Parameter(Mandatory = $true)][string]$Replacement,
        [Parameter(Mandatory = $true)][string]$FileName
    )
    $first = $Content.IndexOf($Anchor, [StringComparison]::Ordinal)
    if ($first -lt 0) {
        throw "${FileName}: required source fragment not found: $Anchor"
    }
    $second = $Content.IndexOf($Anchor, $first + $Anchor.Length, [StringComparison]::Ordinal)
    if ($second -ge 0) {
        throw "${FileName}: source fragment occurs more than once: $Anchor"
    }
    return $Content.Substring(0, $first) + $Replacement + $Content.Substring($first + $Anchor.Length)
}

function Assert-Count {
    param([string]$Content, [string]$Anchor, [int]$Expected, [string]$FileName)
    $count = 0
    $offset = 0
    while ($true) {
        $found = $Content.IndexOf($Anchor, $offset, [StringComparison]::Ordinal)
        if ($found -lt 0) { break }
        $count++
        $offset = $found + $Anchor.Length
    }
    if ($count -ne $Expected) {
        throw "${FileName}: expected $Expected instances, found $count of: $Anchor"
    }
}

try {
    $repoRoot = (Resolve-Path -LiteralPath $Repo -ErrorAction Stop).ProviderPath
    if (-not (Test-Path -LiteralPath (Join-Path $repoRoot 'XR_3DA.sln') -PathType Leaf)) {
        throw 'Repository root not found: XR_3DA.sln is missing.'
    }

    # ISO-8859-1 is a byte-for-byte round-trip for unmodified source bytes.
    # This is the same encoding technique used by apply_experiment.py.
    $byteEncoding = [Text.Encoding]::GetEncoding(28591)
    $sourceFiles = @(
        'xr_3da/xrRender/xrRender_console.h',
        'xr_3da/xrRender/xrRender_console.cpp',
        'xr_3da/xrRender_R2/r2_rendertarget.h',
        'xr_3da/xrRender_R2/r2_rendertarget.cpp',
        'xr_3da/xrRender_R2/r2_rendertarget_accum_direct.cpp'
    )
    $shaderFiles = @(
        'resources/shaders/r2/accum_volumetric_sun.ps',
        'resources/shaders/r2/accum_volumetric_sun.s'
    )
    $fileOrder = @($sourceFiles) + @($shaderFiles)
    $sources = @{}
    $windowsNewline = @{}
    $pending = @{}

    # Load and validate everything before any write.
    foreach ($name in $sourceFiles) {
        $path = Join-Path $repoRoot $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Missing C++ source file: $name"
        }
        [byte[]]$raw = [IO.File]::ReadAllBytes($path)
        $decoded = $byteEncoding.GetString($raw)
        $windowsNewline[$name] = $decoded.Contains("`r`n")
        $sources[$name] = $decoded.Replace("`r`n", "`n")
    }

    $name = $sourceFiles[0]
    $content = $sources[$name]
    $anchor = 'extern ECORE_API float ps_r2_soft_particles_distance;'
    $sources[$name] = Replace-One $content $anchor ($anchor + "`nextern ECORE_API int ps_r2_sun_shafts;`nextern ECORE_API float ps_r2_sun_shafts_intensity;") $name

    $name = $sourceFiles[1]
    $content = $sources[$name]
    $anchor = 'float ps_r2_soft_particles_distance = 0.5f; // metres in view space'
    $content = Replace-One $content $anchor ($anchor + "`nint ps_r2_sun_shafts = 0; // 0 disabled, 1=12, 2=20, 3=40 shadow samples`nfloat ps_r2_sun_shafts_intensity = 0.6f;") $name
    $anchor = 'CMD3(CCC_Mask, "r2_sun_stable", &ps_r2_ls_flags, R2FLAG_SUN_STABLE);'
    $content = Replace-One $content $anchor ($anchor + "`n`tCMD4(CCC_Integer, `"r2_sun_shafts`", &ps_r2_sun_shafts, 0, 3);`n`tCMD4(CCC_Float, `"r2_sun_shafts_intensity`", &ps_r2_sun_shafts_intensity, 0.f, 2.f);") $name
    $sources[$name] = $content

    $name = $sourceFiles[2]
    $content = $sources[$name]
    $anchor = "`tref_shader s_accum_direct;"
    $content = Replace-One $content $anchor ($anchor + "`n`tref_shader s_accum_direct_volumetric;") $name
    $anchor = "`tvoid accum_direct(u32 sub_phase);"
    $content = Replace-One $content $anchor ($anchor + "`n`tvoid accum_direct_volumetric(u32 sub_phase, u32 Offset, const Fmatrix& m_shadow,`n`t`tconst Fvector& sun_dir, const Fvector& sun_color);") $name
    $sources[$name] = $content

    $name = $sourceFiles[3]
    $content = $sources[$name]
    $anchor = "`t`t" + 's_accum_direct.create(b_accum_direct, "r2\\accum_direct");'
    Assert-Count $content $anchor 2 $name
    $anchor = "`n`t// POINT`n"
    $content = Replace-One $content $anchor ("`n`t// Sun shafts shader is available but remains disabled by default.`n`t" + 's_accum_direct_volumetric.create("accum_volumetric_sun");' + "`n" + $anchor) $name
    $sources[$name] = $content

    $name = $sourceFiles[4]
    $content = $sources[$name]
    $anchor = "`t`tRCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);`n`n`t`t// Fetch4 : disable"
    $replacement = "`t`tRCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);`n`n`t`t// Add volumetric shafts while this sun cascade's shadow map is bound.`n`t`taccum_direct_volumetric(sub_phase, Offset, m_shadow, L_dir, L_clr);`n`n`t`t// Fetch4 : disable"
    $content = Replace-One $content $anchor $replacement $name
    if ($content.Contains('void CRenderTarget::accum_direct_volumetric(')) {
        throw "${name}: shafts implementation already present; do not apply the patch twice."
    }

    $method = @'
// Experiment 1: shadow-map based sun shafts, adapted from Clear Sky DX9 R2.
// Keep the original sun lighting path unchanged when the effect is disabled.
void CRenderTarget::accum_direct_volumetric(u32 sub_phase, u32 Offset, const Fmatrix& m_shadow,
                                            const Fvector& sun_dir, const Fvector& sun_color)
{
    if (ps_r2_sun_shafts == 0 || ps_r2_sun_shafts_intensity < 0.0001f ||
        !RImplementation.o.fp16_blend)
        return;
    if (sub_phase != SE_SUN_NEAR && sub_phase != SE_SUN_FAR)
        return;

    // The stock SoC accumulator lacks Clear Sky's separate volume RT.
    // We add the shafts to the existing FP16 HDR light buffer instead.
    phase_accumulator();
    RCache.set_CullMode(CULL_NONE);
    RCache.set_ColorWriteEnable();

    // Slot zero is the shadow map (see shadow.h); match the active R2 backend.
    LPCSTR smap_name = RImplementation.o.HW_smap ? r2_RT_smap_depth : r2_RT_smap_surf;
    STextureList* textures = &*s_accum_direct_volumetric->E[0]->passes[0]->T;
    for (STextureList::iterator it = textures->begin(); it != textures->end(); ++it)
    {
        if (it->first == 0)
            it->second.create(smap_name);
    }

    const float samples = (ps_r2_sun_shafts == 1) ? 12.f :
                          (ps_r2_sun_shafts == 2) ? 20.f : 40.f;
    extern float OLES_SUN_LIMIT_27_01_07;
    const float cascade_start = (sub_phase == SE_SUN_NEAR) ? 0.f : ps_r2_sun_near;
    const float cascade_end = (sub_phase == SE_SUN_NEAR) ? ps_r2_sun_near : OLES_SUN_LIMIT_27_01_07;

    RCache.set_Element(s_accum_direct_volumetric->E[0]);
    RCache.set_Geometry(g_combine_2UV);
    RCache.set_Stencil(FALSE);
    RCache.set_c("m_shadow", m_shadow);
    RCache.set_c("Ldynamic_dir", sun_dir.x, sun_dir.y, sun_dir.z, 0.f);
    RCache.set_c("Ldynamic_color", sun_color.x, sun_color.y, sun_color.z, 0.f);
    RCache.set_c("kich_sun_shafts_params", samples, ps_r2_sun_shafts_intensity,
                 cascade_start, cascade_end);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    // Restore the stencil contract expected by the remaining lighting passes.
    RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, dwLightMarkerID, 0xff, 0x00);
}
'@
    $sources[$name] = $content.TrimEnd() + "`n`n" + $method + "`n"

    foreach ($name in $sourceFiles) {
        $text = $sources[$name]
        if ($windowsNewline[$name]) { $text = $text.Replace("`n", "`r`n") }
        $pending[$name] = $byteEncoding.GetBytes($text)
    }
    foreach ($name in $shaderFiles) {
        $shaderPath = Join-Path $PSScriptRoot $name
        if (-not (Test-Path -LiteralPath $shaderPath -PathType Leaf)) {
            throw "Missing shader supplied with the patch: $name"
        }
        $pending[$name] = [IO.File]::ReadAllBytes($shaderPath)
    }

    if ($Check) {
        Write-Host 'OK: all source fragments and supplied shaders verified. No files changed.'
        Write-Host 'Files to modify/create:'
        foreach ($name in $fileOrder) { Write-Host "  $name" }
        exit 0
    }

    $stamp = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
    $backupRoot = Join-Path (Join-Path $repoRoot '_experiment_backups') "ex1_$stamp"
    $original = @{}
    foreach ($name in $fileOrder) {
        $target = Join-Path $repoRoot $name
        if (Test-Path -LiteralPath $target -PathType Leaf) {
            $original[$name] = [IO.File]::ReadAllBytes($target)
        } else {
            $original[$name] = $null
        }
    }
    # Back up every existing target before touching any source or shader.
    foreach ($name in $fileOrder) {
        if ($null -ne $original[$name]) {
            $dest = Join-Path $backupRoot $name
            [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest))
            [IO.File]::WriteAllBytes($dest, $original[$name])
        }
    }
    $touched = (New-Object "System.Collections.Generic.List[string]")
    try {
        foreach ($name in $fileOrder) {
            $dest = Join-Path $repoRoot $name
            [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest))
            [void]$touched.Add($name)
            [IO.File]::WriteAllBytes($dest, $pending[$name])
        }
    } catch {
        $writeError = $_.Exception.Message
        $restoreErrors = (New-Object "System.Collections.Generic.List[string]")
        foreach ($name in $touched) {
            try {
                $dest = Join-Path $repoRoot $name
                if ($null -eq $original[$name]) {
                    if (Test-Path -LiteralPath $dest) { Remove-Item -LiteralPath $dest -Force }
                } else {
                    [IO.File]::WriteAllBytes($dest, $original[$name])
                }
            } catch {
                [void]$restoreErrors.Add("$name : $($_.Exception.Message)")
            }
        }
        if ($restoreErrors.Count -gt 0) {
            throw ("Write failed: $writeError. Rollback errors: " + ($restoreErrors -join '; ') + ". Backups: $backupRoot")
        }
        throw "Write failed; source state restored. Error: $writeError"
    }

    Write-Host 'EX1 patch applied successfully.'
    Write-Host "Backup folder: $backupRoot"
    Write-Host 'Next: build xrGame and copy resources/shaders/r2/accum_volumetric_sun.* to game gamedata/shaders/r2/'
    exit 0
}
catch {
    [Console]::Error.WriteLine("EX1 ERROR: " + $_.Exception.Message)
    exit 2
}
