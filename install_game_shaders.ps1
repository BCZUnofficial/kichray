# Installs EX1.2 .ps/.s to the *game*, not the source repository.
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Game)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
try {
    $root = (Resolve-Path -LiteralPath $Game -ErrorAction Stop).ProviderPath
    $targetDir = Join-Path $root 'gamedata/shaders/r2'
    if (-not (Test-Path -LiteralPath $targetDir -PathType Container)) {
        throw "Game's gamedata/shaders/r2 directory not found: $targetDir"
    }
    $shaderNames = @('accum_volumetric_sun.ps', 'accum_volumetric_sun.s',
                     'kich_sun_shafts_combine.ps', 'kich_sun_shafts_combine.s')
    $backup = Join-Path $root ("_ex1_2_shader_backups/" + (Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
    foreach ($name in $shaderNames) {
        $src = Join-Path $PSScriptRoot ("resources/shaders/r2/" + $name)
        if (-not [IO.File]::Exists($src)) { throw "Missing patch shader: $src" }
    }
    foreach ($name in $shaderNames) {
        $old = Join-Path $targetDir $name
        if ([IO.File]::Exists($old)) {
            [void][IO.Directory]::CreateDirectory($backup)
            [IO.File]::Copy($old, (Join-Path $backup $name), $true)
        }
    }
    foreach ($name in $shaderNames) {
        $src = Join-Path $PSScriptRoot ("resources/shaders/r2/" + $name)
        [IO.File]::Copy($src, (Join-Path $targetDir $name), $true)
    }
    Write-Host "EX1.2 game shaders copied to: $targetDir"
    Write-Host "Backups of previous game shaders (if any): $backup"
}
catch {
    [Console]::Error.WriteLine('EX1.2 GAME SHADERS ERROR: ' + $_.Exception.Message)
    exit 2
}
