<#
.SYNOPSIS
    Checks that the Crowdy SDK mount at Plugins/Crowdy is pinned, synced, clean and detached, and that no tracked
    project file names the SDK's on-disk path.

.DESCRIPTION
    The SDK is the crowdy-sdk Diversion repo, mounted at Plugins/Crowdy and pinned to one commit. Four things go wrong
    quietly around it, and this checks each one:

      - The mount is not synced to the pin (pin_mismatch), so the editor builds a different SDK than the branch names.
      - Files inside the mount were edited. The editor shows no source-control state there, so a save is silent, and it
        blocks the next repin.
      - The mount sits on a branch instead of detached at the pin, so it follows whatever lands on that branch.
      - A build input (Source, Config, Content, a Construct plugin, the .uproject) names Plugins/Crowdy or
        Plugins/CrowdySDK. The SDK sits at a different path on the GitHub mirror, so such a file builds on one side
        only. Refer to the plugin and its modules by name instead. Docs and these scripts may name the mount.

    It changes nothing. Exits 0 when every check passes and 1 otherwise, so it can gate a merge or a publish.

.PARAMETER RepoRoot
    The workspace root to check. Defaults to the folder above this script.

.PARAMETER PathsOnly
    Check only that no build input names the SDK's path, for a workspace that never mounts the SDK (the publish
    script's).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\check-sdk-pin.ps1
#>

[CmdletBinding()]
param(
    [string] $RepoRoot,
    [switch] $PathsOnly
)

$ErrorActionPreference = "Stop"

if (-not $RepoRoot) { $RepoRoot = Split-Path -Parent $PSScriptRoot }
$MountPath = Join-Path $RepoRoot "Plugins\Crowdy"

$results = @()
function Add-Result([string]$Name, [bool]$Ok, [string]$Detail) {
    $script:results += [pscustomobject]@{ Name = $Name; Ok = $Ok; Detail = $Detail }
}

Write-Host "SDK pin check" -ForegroundColor Cyan
Write-Host "  repo : $RepoRoot"
Write-Host ""

if (-not $PathsOnly) {
    Push-Location $RepoRoot
    try { $statusText = (dv submodule status 2>&1) -join "`n" }
    finally { Pop-Location }

    $row = ($statusText -split "`r?`n") | Where-Object { $_ -match '^\s*Plugins/Crowdy\s' } | Select-Object -First 1
    if (-not $row) {
        Add-Result "submodule" $false "no submodule at Plugins/Crowdy (dv submodule status printed: $($statusText.Trim()))"
    }
    else {
        $cols = $row.Trim() -split '\s+'
        $repo = $cols[1]; $pin = $cols[2]; $state = $cols[3]; $localChanges = $cols[4]
        Add-Result "repo" ($repo -eq "crowdy-sdk") "$repo"
        Add-Result "pin" ($pin -match '^dv\.commit\.\d+$') "$pin"
        Add-Result "state" ($state -eq "synced") "$state"
        Add-Result "local changes" ($localChanges -eq "0") "$localChanges"
    }

    if (Test-Path (Join-Path $MountPath ".diversion")) {
        Push-Location $MountPath
        try { $childText = (dv status 2>&1) -join "`n" }
        finally { Pop-Location }
        Add-Result "detached" ($childText -match "Detached from branch") $(if ($childText -match "On branch (\S+)") { "on branch $($Matches[1])" } else { "detached at the pin" })
        Add-Result "mount clean" ($childText -match "Your workspace has no changes") $(if ($childText -match "Your workspace has no changes") { "no changes" } else { "changes inside Plugins/Crowdy" })
    }
    else {
        Add-Result "mount present" $false "Plugins/Crowdy is not a mounted workspace; run scripts\sync-sdk.ps1"
    }
}

$scanRoots = @()
foreach ($dir in @("Source", "Config", "Content")) {
    $p = Join-Path $RepoRoot $dir
    if (Test-Path $p) { $scanRoots += $p }
}
$pluginsDir = Join-Path $RepoRoot "Plugins"
if (Test-Path $pluginsDir) {
    $scanRoots += Get-ChildItem $pluginsDir -Directory | Where-Object { $_.Name -ne "Crowdy" } | ForEach-Object { $_.FullName }
}

$textTypes = @("*.cs", "*.h", "*.hpp", "*.cpp", "*.inl", "*.ini", "*.uplugin", "*.json", "*.py")
$skipDirs = '\\(Binaries|Intermediate|Saved|DerivedDataCache)\\'
$files = @()
foreach ($root in $scanRoots) {
    $files += Get-ChildItem -Path $root -Recurse -File -Include $textTypes -ErrorAction SilentlyContinue |
              Where-Object { $_.FullName -notmatch $skipDirs }
}
$files += Get-ChildItem -Path $RepoRoot -File -Filter "*.uproject"

$hits = @()
foreach ($f in $files) {
    $found = Select-String -Path $f.FullName -Pattern 'Plugins[\\/]+Crowdy' -SimpleMatch:$false -ErrorAction SilentlyContinue
    foreach ($h in $found) { $hits += "$($f.FullName.Substring($RepoRoot.Length).TrimStart('\')):$($h.LineNumber)" }
}
Add-Result "SDK path in project files" ($hits.Count -eq 0) $(if ($hits.Count -eq 0) { "none in $($files.Count) file(s)" } else { $hits -join ", " })

$failed = @($results | Where-Object { -not $_.Ok })
foreach ($r in $results) {
    $color = if ($r.Ok) { "Green" } else { "Red" }
    $mark = if ($r.Ok) { "ok  " } else { "FAIL" }
    Write-Host ("  {0} {1,-26} {2}" -f $mark, $r.Name, $r.Detail) -ForegroundColor $color
}
Write-Host ""
if ($failed.Count -gt 0) {
    Write-Host "FAILED: $($failed.Count) check(s)" -ForegroundColor Red
    exit 1
}
Write-Host "OK" -ForegroundColor Green
exit 0
