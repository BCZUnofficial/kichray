[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Repo, [switch]$Check)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$enc = [Text.Encoding]::GetEncoding(28591)
$jobs = New-Object System.Collections.ArrayList
function Add-Job([string]$path, [byte[]]$old, [byte[]]$data) {
    if ([Convert]::ToBase64String($old) -ne [Convert]::ToBase64String($data)) {
        [void]$jobs.Add([pscustomobject]@{Path=$path; Old=$old; Data=$data})
    }
}
function Hash([byte[]]$data) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($data))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
}
if ($Repo) {
    $Repo = (Resolve-Path -LiteralPath $Repo).ProviderPath
    $changes = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'source_changes/changes.json') -Raw | ConvertFrom-Json
    foreach ($c in $changes) {
        $path = Join-Path $Repo $c.path
        $old = [IO.File]::ReadAllBytes($path)
        $text = $enc.GetString($old)
        $newline = "`n"
        if ($text.Contains("`r`n")) { $newline = "`r`n" }
        $needle = $c.old.Replace("`n", $newline)
        $replacement = $c.new.Replace("`n", $newline)
        $count = [regex]::Matches($text, [regex]::Escape($needle)).Count
        if ($count -eq 0 -and $text.Contains($replacement)) { Write-Host "Already patched: $path"; continue }
        if ($count -ne 1) { throw "Source differs from supported version: $path. No changes written." }
        Add-Job $path $old ($enc.GetBytes($text.Replace($needle, $replacement)))
    }
}
foreach ($job in $jobs) { Write-Host "Ready: $($job.Path)" }
if ($Check) { Write-Host 'CHECK OK. Nothing written.'; exit 0 }
if ($jobs.Count -eq 0) { Write-Host 'Everything already patched.'; exit 0 }
$stamp = [DateTime]::UtcNow.ToString('yyyyMMdd_HHmmss_ffff')
$backups = @()
foreach ($job in $jobs) {
    $backup = $job.Path + '.before_sun_start_e_' + $stamp
    [IO.File]::WriteAllBytes($backup, $job.Old)
    $backups += $backup
}
try {
    foreach ($job in $jobs) { [IO.File]::WriteAllBytes($job.Path, $job.Data) }
} catch {
    foreach ($job in $jobs) { [IO.File]::WriteAllBytes($job.Path, $job.Old) }
    throw
}
Write-Host 'DONE. Backups:'
$backups | ForEach-Object { Write-Host $_ }
Write-Host 'Build XR_3DA (Release/Win32). Install the rebuilt engine executable. Keep a backup of the previous executable.'
