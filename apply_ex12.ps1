# Kichray EX1.2: migrate an already installed EX1 sun-shafts experiment
# to a separate half-resolution volume RT and pre-forward compositing.
# Requires PowerShell 5.1+; no Python needed.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Repo,
    [switch]$Check
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Replace-One {
    param([string]$Content, [string]$Anchor, [string]$Replacement, [string]$FileName)
    $start = $Content.IndexOf($Anchor, [StringComparison]::Ordinal)
    if ($start -lt 0) { throw "${FileName}: anchor not found: $Anchor" }
    $again = $Content.IndexOf($Anchor, $start + $Anchor.Length, [StringComparison]::Ordinal)
    if ($again -ge 0) { throw "${FileName}: duplicate anchor: $Anchor" }
    return $Content.Substring(0, $start) + $Replacement + $Content.Substring($start + $Anchor.Length)
}
function Read-Payload {
    param([string]$RelPath)
    $path = Join-Path $PSScriptRoot $RelPath
    if (-not [IO.File]::Exists($path)) { throw "Patch file missing: $RelPath" }
    return [IO.File]::ReadAllText($path)
}
try {
    $root = (Resolve-Path -LiteralPath $Repo -ErrorAction Stop).ProviderPath
    if (-not (Test-Path -LiteralPath (Join-Path $root 'XR_3DA.sln') -PathType Leaf)) {
        throw 'Not the Kichray repository root: XR_3DA.sln not found.'
    }
    $cppFiles = @(
        'xr_3da/xrRender_R2/r2_rendertarget.h',
        'xr_3da/xrRender_R2/r2_rendertarget.cpp',
        'xr_3da/xrRender_R2/r2_rendertarget_accum_direct.cpp',
        'xr_3da/xrRender_R2/r2_rendertarget_phase_combine.cpp',
        'xr_3da/xrRender_R2/r2_rendertarget_phase_scene.cpp'
    )
    $shaderFiles = @(
        'resources/shaders/r2/accum_volumetric_sun.ps',
        'resources/shaders/r2/accum_volumetric_sun.s',
        'resources/shaders/r2/kich_sun_shafts_combine.ps',
        'resources/shaders/r2/kich_sun_shafts_combine.s'
    )
    $allFiles = @($cppFiles) + @($shaderFiles)
    # Latin-1 roundtrips every original byte unchanged (including CP1251 comments).
    $byteEncoding = [Text.Encoding]::GetEncoding(28591)
    $s = @{}
    $crlf = @{}
    $pending = @{}
    foreach ($file in $cppFiles) {
        $path = Join-Path $root $file
        if (-not [IO.File]::Exists($path)) { throw "C++ file not found: $file" }
        $txt = $byteEncoding.GetString([IO.File]::ReadAllBytes($path))
        $crlf[$file] = $txt.Contains("`r`n")
        $s[$file] = $txt.Replace("`r`n", "`n")
    }
    $file = $cppFiles[0]
    $txt = $s[$file]
    if (-not $txt.Contains('s_accum_direct_volumetric')) {
        throw "${file}: EX1 not installed. Install EX1 first."
    }
    if ($txt.Contains('rt_SunShafts')) { throw 'EX1.2 already installed; no files changed.' }
    $a = "`tu32 dwAccumulatorClearMark;"
    $txt = Replace-One $txt $a ($a + "`n`tu32 dwShaftsClearMark;") $file
    $a = "`tref_rt rt_Generic_1;"
    $txt = Replace-One $txt $a ($a + "`n`tref_rt rt_SunShafts; // EX1.2 separate half-res volume accumulation") $file
    $a = "`tref_shader s_accum_direct_volumetric;"
    $txt = Replace-One $txt $a ($a + "`n`tref_shader s_sun_shafts_combine;") $file
    $a = "`tvoid phase_combine();"
    $txt = Replace-One $txt $a ($a + "`n`tvoid phase_combine_sun_shafts();") $file
    $s[$file] = $txt

    $file = $cppFiles[1]
    $txt = $s[$file]
    $a = "`tdwAccumulatorClearMark = 0;"
    $txt = Replace-One $txt $a ($a + "`n`tdwShaftsClearMark = 0;") $file
    $a = "`t`trt_Generic_1.create(r2_RT_generic1, w, h, D3DFMT_A8R8G8B8);"
    $r = $a + "`n`t`t// Half linear resolution = one quarter of full-resolution pixels.`n" +
         "`t`trt_SunShafts.create(`"`$user`$kich_sun_shafts`", _max(1u, w / 2), _max(1u, h / 2), D3DFMT_A8R8G8B8);"
    $txt = Replace-One $txt $a $r $file
    $a = "`ts_accum_direct_volumetric.create(`"accum_volumetric_sun`");"
    $txt = Replace-One $txt $a ($a + "`n`ts_sun_shafts_combine.create(`"kich_sun_shafts_combine`");") $file
    $s[$file] = $txt

    $file = $cppFiles[2]
    $txt = $s[$file]
    $marker = '// Experiment 1: shadow-map based sun shafts, adapted from Clear Sky DX9 R2.'
    $at = $txt.IndexOf($marker, [StringComparison]::Ordinal)
    if ($at -lt 0) { throw "${file}: expected EX1 source implementation not found." }
    if ($txt.IndexOf($marker, $at + $marker.Length, [StringComparison]::Ordinal) -ge 0) {
        throw "${file}: duplicate EX1 methods found; aborting."
    }
    if (-not $txt.Substring($at).Contains('void CRenderTarget::accum_direct_volumetric(')) {
        throw "${file}: EX1 method signature not found; aborting."
    }
    $replacement = (Read-Payload 'new_accum_method.cpp.txt').Replace("`r`n", "`n")
    $s[$file] = $txt.Substring(0, $at).TrimEnd() + "`n`n" + $replacement.TrimEnd() + "`n"

    $file = $cppFiles[3]
    $txt = $s[$file]
    $a = "`t}`n`n`t// Forward rendering"
    $r = "`t}`n`n`t// EX1.2 volume texture is composited into generic0 before forward objects.`n" +
         "`tphase_combine_sun_shafts();`n`n`t// Forward rendering"
    $txt = Replace-One $txt $a $r $file
    $method = (Read-Payload 'new_combine_method.cpp.txt').Replace("`r`n", "`n")
    if ($txt.Contains('void CRenderTarget::phase_combine_sun_shafts(')) {
        throw "${file}: EX1.2 combine method already defined."
    }
    $s[$file] = $txt.TrimEnd() + "`n`n" + $method.TrimEnd() + "`n"

    $file = $cppFiles[4]
    $txt = $s[$file]
    # Empty sky G-buffer position is essential; existing soft-particles behaviour is preserved.
    $a = 'if (ps_r2_ls_flags.test(R2FLAG_SOFT_PARTICLES))'
    $r = 'if (ps_r2_ls_flags.test(R2FLAG_SOFT_PARTICLES) || ps_r2_sun_shafts > 0)'
    $s[$file] = Replace-One $txt $a $r $file

    foreach ($file in $cppFiles) {
        $txt = $s[$file]
        if ($crlf[$file]) { $txt = $txt.Replace("`n", "`r`n") }
        $pending[$file] = $byteEncoding.GetBytes($txt)
    }
    foreach ($file in $shaderFiles) {
        $path = Join-Path $PSScriptRoot $file
        if (-not [IO.File]::Exists($path)) { throw "Shader payload not found: $file" }
        $pending[$file] = [IO.File]::ReadAllBytes($path)
    }
    if ($Check) {
        Write-Host 'OK: EX1 was found. EX1.2 source anchors and payload verified; nothing changed.'
        foreach ($file in $allFiles) { Write-Host "  $file" }
        exit 0
    }
    $stamp = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
    $backup = Join-Path $root ("_experiment_backups/ex1_2_" + $stamp)
    $original = @{}
    foreach ($file in $allFiles) {
        $path = Join-Path $root $file
        if ([IO.File]::Exists($path)) { $original[$file] = [IO.File]::ReadAllBytes($path) }
        else { $original[$file] = $null }
    }
    # Verify all backups can be written BEFORE overwriting any target.
    foreach ($file in $allFiles) {
        if ($null -ne $original[$file]) {
            $path = Join-Path $backup $file
            [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
            [IO.File]::WriteAllBytes($path, $original[$file])
        }
    }
    $touched = New-Object 'System.Collections.Generic.List[string]'
    try {
        foreach ($file in $allFiles) {
            $path = Join-Path $root $file
            [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
            [void]$touched.Add($file)
            [IO.File]::WriteAllBytes($path, $pending[$file])
        }
    }
    catch {
        $writeError = $_.Exception.Message
        $restoreErrors = New-Object 'System.Collections.Generic.List[string]'
        foreach ($file in $touched) {
            try {
                $path = Join-Path $root $file
                if ($null -eq $original[$file]) { Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue }
                else { [IO.File]::WriteAllBytes($path, $original[$file]) }
            }
            catch { [void]$restoreErrors.Add("${file}: $($_.Exception.Message)") }
        }
        if ($restoreErrors.Count -gt 0) {
            throw "Write failed: $writeError; rollback also failed for: $($restoreErrors -join '; '); backups: $backup"
        }
        throw "Write failed. Changes rolled back. $writeError"
    }
    Write-Host 'EX1.2 installed in local source tree.'
    Write-Host "Backups: $backup"
    Write-Host 'Next: build xrRender_R2, replace the renderer DLL, and copy all four EX1.2 shaders to game gamedata/shaders/r2.'
    exit 0
}
catch {
    [Console]::Error.WriteLine('EX1.2 ERROR: ' + $_.Exception.Message)
    exit 2
}
