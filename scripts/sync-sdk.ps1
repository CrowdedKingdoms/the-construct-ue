<#
.SYNOPSIS
    Brings this workspace's Plugins/Crowdy mount in line with the SDK commit the current branch pins.

.DESCRIPTION
    The Crowdy SDK is not in this repo. It lives in the crowdy-sdk Diversion repo and is mounted at Plugins/Crowdy,
    pinned to a commit. Syncing this repo does NOT move the mount, so after any change that moves the pin the mount has
    to be updated separately. This script does that, plus the things that silently go wrong around it.

    It is safe to run at any time. Every step is skipped when it is already true, so running it when nothing changed
    just prints a clean report and exits 0.

    What it handles that a bare 'dv submodule update' does not:

      - An open editor or IDE on this workspace. Both hold locks on the files this deletes.
      - Uncommitted work inside the mount. 'dv submodule update' refuses a dirty mount, and the editor shows no
        source-control state for those files, so nothing warns you that you saved one. This lists the files and stops.
      - A mount that has drifted onto a branch. It is meant to sit detached at the pin; on a branch it follows that
        branch, and 'dv submodule update' refuses to move it.
      - Stale Intermediate and Binaries after the pin moves, which fail the next build with include errors that look
        like code faults.
      - "Repo ... not found", which means you have no access to crowdy-sdk, not that the mount is broken.

.PARAMETER Build
    Build TheConstructEditor afterwards and require real Compile or Link lines. Off by default because it is slow.

.PARAMETER KeepIntermediate
    Do not delete Intermediate and Binaries even when the pin moved.

.PARAMETER DiscardMountChanges
    Discard anything modified inside Plugins/Crowdy instead of stopping. Use only for accidental saves; a change worth
    keeping is SDK work, made in the crowdy-sdk repo.

.PARAMETER EngineRoot
    Unreal Engine install root, the folder containing Engine\. Omit it to use $env:UE_ROOT, then the engine the
    .uproject names, then the newest install found.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\sync-sdk.ps1 -Build

.NOTES
    Run from PowerShell, never from Git Bash: bash mangles the spaced engine path and the build step silently no-ops.
    Exits non-zero on any failure, so it is usable as a gate.
#>

[CmdletBinding()]
param(
    [switch] $Build,
    [switch] $KeepIntermediate,
    [switch] $DiscardMountChanges,
    [string] $EngineRoot
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "Resolve-EngineRoot.ps1")

$RepoRoot    = Split-Path -Parent $PSScriptRoot
$ProjectFile = Join-Path $RepoRoot "TheConstruct.uproject"
$MountPath   = Join-Path $RepoRoot "Plugins\Crowdy"
$EngineRoot  = Resolve-EngineRoot -Explicit $EngineRoot -ProjectFile $ProjectFile
$BuildBat    = if ($EngineRoot) { Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat" } else { $null }

$steps = @()
function Add-Step([string]$Name, [string]$Detail) { $script:steps += [pscustomobject]@{ Name = $Name; Detail = $Detail } }
function Fail([string]$Message) {
    Write-Host ""
    Write-Host "FAILED: $Message" -ForegroundColor Red
    exit 1
}

Write-Host "SDK mount sync" -ForegroundColor Cyan
Write-Host "  repo : $RepoRoot"
Write-Host "  mount: $MountPath"
Write-Host ""

if (-not (Test-Path $ProjectFile)) {
    Fail "This is not the-construct-ue workspace root. Run the script from the repo's scripts folder."
}
if (-not $BuildBat -or -not (Test-Path $BuildBat)) {
    Fail "No Unreal Engine build script found$(if ($EngineRoot) { " under '$EngineRoot'" }). Pass -EngineRoot or set UE_ROOT."
}

Push-Location $RepoRoot
try {
    $mountPresent = Test-Path (Join-Path $MountPath ".diversion")
    $statusBefore = (dv submodule status 2>&1) -join "`n"
    $needsUpdate  = (-not $mountPresent) -or ($statusBefore -notmatch "synced")

    if ($needsUpdate) {
        $blockers = Get-CimInstance Win32_Process -Filter "Name LIKE 'UnrealEditor%' OR Name = 'devenv.exe' OR Name = 'rider64.exe'" -ErrorAction SilentlyContinue |
            Where-Object { -not $_.CommandLine -or ($_.CommandLine -replace '/', '\').IndexOf($RepoRoot, [System.StringComparison]::OrdinalIgnoreCase) -ge 0 }
        if ($blockers) {
            $names = ($blockers | ForEach-Object { "$($_.Name) (pid $($_.ProcessId))" }) -join ", "
            Fail "There is work to do and these hold file locks. Close them and re-run: $names"
        }
        Add-Step "editors closed" "none running"
    }
    else {
        Add-Step "editors" "not checked, nothing to change"
    }

    $commitBefore = $null
    if ($mountPresent) {
        Push-Location $MountPath
        try {
            $childStatus = (dv status --no-limit 2>&1) -join "`n"
            if ($childStatus -notmatch "Your workspace has no changes") {
                $dirty = [regex]::Matches($childStatus, '(?m)^\s+([^\r\n]+)$') |
                         ForEach-Object { $_.Groups[1].Value.Trim() } |
                         Where-Object { $_ -and $_ -notmatch '^(New|Modified|Deleted|Total|Local|Cloud|Working|In repo|On branch|Detached)' }
                if ($dirty) {
                    if (-not $DiscardMountChanges) {
                        Write-Host "The mount has uncommitted changes, which blocks the update:" -ForegroundColor Yellow
                        $dirty | Select-Object -First 20 | ForEach-Object { Write-Host "    $_" -ForegroundColor Yellow }
                        Write-Host ""
                        Write-Host "  The editor shows no source-control state under the mount, so these are often accidental saves." -ForegroundColor Yellow
                        Write-Host "  Ask before discarding them (an agent asks its developer); once agreed, re-run with" -ForegroundColor Yellow
                        Write-Host "  -DiscardMountChanges. A change worth keeping is SDK work: file it as an SDK gap (AGENTS.md section 1)." -ForegroundColor Yellow
                        Fail "Uncommitted changes inside Plugins/Crowdy. Nothing was changed."
                    }
                    dv reset --all -f --clean 2>&1 | Out-Null
                    if ($LASTEXITCODE -ne 0) { Fail "dv reset inside Plugins/Crowdy returned $LASTEXITCODE" }
                    if (((dv status --no-limit 2>&1) -join "`n") -notmatch "Your workspace has no changes") {
                        Fail "Changes are still inside Plugins/Crowdy after the reset. Run dv status there."
                    }
                    Add-Step "mount changes" "discarded $($dirty.Count) file(s)"
                }
            }
            else {
                Add-Step "mount changes" "none"
            }
            if ($childStatus -match "(dv\.commit\.\d+)") { $commitBefore = $Matches[1] }
        }
        finally { Pop-Location }
    }
    else {
        Add-Step "mount changes" "mount not present yet, will be fetched"
    }

    if ($mountPresent -and $statusBefore -match 'dv\.commit\.\d+') {
        $pinned = $Matches[0]
        Push-Location $MountPath
        try {
            $childState = (dv status 2>&1) -join "`n"
            if ($childState -notmatch "Detached from branch") {
                $branch = if ($childState -match "On branch (\S+)") { $Matches[1] } else { "a branch" }
                Write-Host "The mount is on $branch instead of detached at $pinned." -ForegroundColor Yellow
                dv checkout $pinned --discard-changes 2>&1 | Select-Object -Last 1 | ForEach-Object { Write-Host "  $_" }
                if ($LASTEXITCODE -ne 0) { Fail "Could not detach the mount at $pinned." }
                Add-Step "mount detached" "was on $branch, now at $pinned"
            }
            else {
                Add-Step "mount attachment" "detached, as it should be"
            }
        }
        finally { Pop-Location }
    }

    Write-Host "Updating the submodule ..." -ForegroundColor Cyan
    $updateOut = dv submodule update 2>&1
    $updateCode = $LASTEXITCODE
    $updateOut | Select-Object -Last 3 | ForEach-Object { Write-Host "  $_" }

    if ($updateCode -ne 0) {
        if (($updateOut -join "`n") -match "Repo (dv\.repo\.[0-9a-f-]+) not found") {
            $repoId = $Matches[1]
            Write-Host ""
            Write-Host "You do not have access to the crowdy-sdk repo ($repoId)." -ForegroundColor Yellow
            Write-Host "  Access to this repo does not grant it: the SDK is a separate Diversion repo." -ForegroundColor Yellow
            Write-Host "  Ask the repo owner for an invite to crowdy-sdk, accept it with" -ForegroundColor Yellow
            Write-Host "      dv invite accept $repoId" -ForegroundColor Yellow
            Write-Host "  and re-run this script." -ForegroundColor Yellow
            Fail "No access to the submodule's repo. Nothing was changed."
        }
        Fail "dv submodule update returned $updateCode"
    }

    $statusText = (dv submodule status 2>&1) -join "`n"
    Write-Host ""
    Write-Host $statusText
    $row = ($statusText -split "`r?`n") | Where-Object { $_ -match '^\s*Plugins/Crowdy\s' } | Select-Object -First 1
    $cols = if ($row) { @($row.Trim() -split '\s+') } else { @() }
    if ($cols.Count -lt 5 -or $cols[3] -ne "synced" -or $cols[4] -ne "0") {
        Fail "The mount is not synced with 0 local changes. Read the columns above."
    }
}
finally { Pop-Location }

$commitAfter = $null
Push-Location $MountPath
try {
    $after = (dv status 2>&1) -join "`n"
    if ($after -notmatch "Detached from branch") { Fail "The mount is not detached at the pin. Run dv status inside Plugins/Crowdy." }
    if ($after -match "(dv\.commit\.\d+)") { $commitAfter = $Matches[1] }
}
finally { Pop-Location }

$pinMoved = $commitBefore -ne $commitAfter
Add-Step "submodule" $(if ($pinMoved) { "$commitBefore -> $commitAfter" } else { "already at $commitAfter" })

if ($pinMoved -and -not $KeepIntermediate) {
    foreach ($d in @("Intermediate", "Binaries")) {
        $p = Join-Path $RepoRoot $d
        if (Test-Path $p) { Remove-Item -Recurse -Force $p -ErrorAction SilentlyContinue }
    }
    Add-Step "stale build output" "deleted (pin moved)"

    Write-Host ""
    Write-Host "Regenerating project files ..." -ForegroundColor Cyan
    & $BuildBat -projectfiles -project="$ProjectFile" -game -rocket -progress 2>&1 |
        Select-Object -Last 2 | ForEach-Object { Write-Host "  $_" }
    if ($LASTEXITCODE -ne 0) { Fail "Project file generation returned $LASTEXITCODE" }
    Add-Step "project files" "regenerated"
}
else {
    Add-Step "stale build output" $(if ($KeepIntermediate) { "kept (-KeepIntermediate)" } else { "untouched, pin did not move" })
}

if ($Build) {
    Write-Host ""
    Write-Host "Building TheConstructEditor ..." -ForegroundColor Cyan
    $out = & $BuildBat TheConstructEditor Win64 Development -Project="$ProjectFile" -WaitMutex 2>&1
    $buildCode = $LASTEXITCODE
    $compiled = @($out | Select-String -Pattern "Compile \[x64\]").Count
    $linked   = @($out | Select-String -Pattern "Link \[x64\]").Count

    if ($buildCode -ne 0) {
        $out | Select-String -Pattern "error|Error:" | Select-Object -First 10 | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
        Fail "Build returned $buildCode after $compiled Compile and $linked Link line(s)"
    }
    Add-Step "build" "$compiled Compile, $linked Link"
    if ($compiled -eq 0 -and $linked -eq 0) {
        Add-Step "build note" "nothing rebuilt, the target was already up to date"
    }
}

Write-Host ""
Write-Host "OK" -ForegroundColor Green
foreach ($s in $steps) { Write-Host ("  {0,-22} {1}" -f $s.Name, $s.Detail) }
if (-not $Build) {
    Write-Host ""
    Write-Host "Not built. Open the editor, or re-run with -Build, before assuming this workspace is good." -ForegroundColor Yellow
}
exit 0
