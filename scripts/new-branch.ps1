<#
.SYNOPSIS
    Cuts a work branch from an up-to-date dev, after checking the name, the workspace and the base.

.DESCRIPTION
    A work branch must start at dev's head, with a name that follows the branch rules and is not already taken. Typed by
    hand, a missed 'dv update' cuts from an old commit and nothing notices. This script checks each step and stops with
    one line at the first one that fails:

      - The name fits the allowed shapes. repin/ and promote/ are the repo owner's, and dev/, test/ and prod/ are tier
        branches, never prefixes.
      - No Unreal editor has this project open, because checkout and update rewrite files it holds open.
      - The workspace has no uncommitted changes, so nothing is carried onto the new branch (see -CarryChanges).
      - No branch of that name exists.
      - dev is checked out and updated, and the workspace commit equals dev's head.
      - After the branch is cut, the workspace sits on it. The base commit is printed.

    It then warns, without failing, when the SDK mount is not synced to the pin the new branch carries.

.PARAMETER Name
    The new branch: <category>/<name> with category feature, fix, chore or docs and one to four lowercase words joined
    by hyphens, or chore/release-starter-<version> or chore/release-construct-<version> for a release, where the
    version is X.Y.Z, X.Y.Z-dev.N or X.Y.Z-test.N.

.PARAMETER CarryChanges
    For changes made on dev by mistake: when the workspace is on dev, at dev's head, and has uncommitted changes, cut
    the branch from there and carry the changes onto it instead of refusing.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\new-branch.ps1 -Name feature/presence-pawn

.NOTES
    Run from PowerShell, never from Git Bash. Exits non-zero on any failure, so it is usable as a gate.
#>

[CmdletBinding()]
param(
    [string] $Name,
    [switch] $CarryChanges
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot

function Fail([string]$Message) {
    Write-Host ""
    Write-Host "FAILED: $Message" -ForegroundColor Red
    exit 1
}

function Get-FirstLine([string]$Text) {
    $line = $Text -split "`r?`n" | Where-Object { $_.Trim() } | Select-Object -First 1
    return "$line".Trim()
}

function Invoke-Dv {
    $out = (& dv @args 2>&1) -join "`n"
    $code = $LASTEXITCODE
    if ($code -ne 0) { Fail "dv $($args -join ' ') returned $code. $($out.Trim() -replace '\s*\r?\n\s*', ' | ')" }
    return $out
}

Write-Host "New work branch" -ForegroundColor Cyan
Write-Host "  repo: $RepoRoot"
Write-Host "  name: $Name"
Write-Host ""

if ($Name -match '^(repin|promote)/') { Fail "'$Name' is not yours to cut: the repo owner cuts these by hand (AGENTS.md 4.2)" }
if ($Name -match '^(dev|test|prod)/') { Fail "'$Name' starts with a tier name; dev, test and prod are tier branches, never prefixes (AGENTS.md 4.1)" }

$shape = '^(feature|fix|chore|docs)/[a-z0-9]+(-[a-z0-9]+){0,3}$'
$release = '^chore/release-(starter|construct)-\d+\.\d+\.\d+(-(dev|test)\.\d+)?$'
if ($Name -cnotmatch $shape -and $Name -cnotmatch $release) {
    Fail "'$Name' is not a valid branch name. Use feature/, fix/, chore/ or docs/ and one to four lowercase words joined by hyphens, or chore/release-starter-<version> (AGENTS.md 4.2)"
}

$editors = Get-CimInstance Win32_Process -Filter "Name LIKE 'UnrealEditor%'" -ErrorAction SilentlyContinue |
    Where-Object { -not $_.CommandLine -or ($_.CommandLine -replace '/', '\').IndexOf($RepoRoot, [System.StringComparison]::OrdinalIgnoreCase) -ge 0 }
if ($editors) {
    Fail "close the editor that has this project open first (AGENTS.md 4.2)"
}

Push-Location $RepoRoot
try {
    $status = Invoke-Dv status
    $dirty = $status -notmatch "Your workspace has no changes"
    $onDev = (Get-FirstLine (Invoke-Dv branch-name)) -eq "dev"
    if ($dirty -and -not ($CarryChanges -and $onDev)) {
        Write-Host "The workspace has changes:" -ForegroundColor Yellow
        $status -split "`r?`n" | Where-Object { $_ -match '^\s+\S' } | Select-Object -First 10 |
            ForEach-Object { Write-Host "    $($_.Trim())" -ForegroundColor Yellow }
        if ($onDev) { Fail "the workspace has pending files on dev: ask your developer about each one; if they were made on dev by mistake, re-run with -CarryChanges (AGENTS.md 4.2)" }
        Fail "the workspace has pending files: ask your developer about each one (commit it on its branch, keep it for later with dv shelf create <name> <paths>, or discard it), then re-run (AGENTS.md 4.2)"
    }

    $branches = Invoke-Dv branch
    if ($branches -match "(?m)^\s*branch $([regex]::Escape($Name)) \(dv\.branch\.\d+\)") {
        Fail "branch '$Name' already exists; pick a new name and never resume an old branch (AGENTS.md 4.2)"
    }

    if (-not $dirty) {
        Write-Host "Updating dev ..." -ForegroundColor Cyan
        if (-not $onDev) { Invoke-Dv checkout dev --ignore-shelf | Out-Null }
        Invoke-Dv update | Out-Null
    }
    $current = Get-FirstLine (Invoke-Dv branch-name)
    if ($current -ne "dev") { Fail "the workspace is on '$current' after checking out dev" }

    $workspaceCommit = Get-FirstLine (Invoke-Dv status --commit-id-only)
    $devHead = Get-FirstLine (Invoke-Dv show dev)
    $devCommit = [regex]::Match($devHead, '^commit (dv\.commit\.\d+)').Groups[1].Value
    if (-not $devCommit) { Fail "could not read dev's head from 'dv show dev': $devHead" }
    if ($workspaceCommit -ne $devCommit) {
        Fail "the workspace is behind dev; run dv update, check dv conflict-backups list, and retry (workspace $workspaceCommit, dev $devCommit)"
    }
    if ($dirty) { Write-Host "Carrying the uncommitted changes onto the new branch" -ForegroundColor Yellow }

    Write-Host "Cutting $Name from $devCommit ..." -ForegroundColor Cyan
    Invoke-Dv branch -c $Name | Out-Null
    $current = Get-FirstLine (Invoke-Dv branch-name)
    if ($current -ne $Name) { Fail "the workspace is on '$current' after cutting $Name" }

    $submodules = (dv submodule status 2>&1) -join "`n"
    $row = $submodules -split "`r?`n" | Where-Object { $_ -match '^\s*Plugins/Crowdy\s' } | Select-Object -First 1
    $cols = if ($row) { @($row.Trim() -split '\s+') } else { @() }
    if ($cols.Count -lt 5 -or $cols[3] -ne "synced" -or $cols[4] -ne "0") {
        Write-Host "Plugins/Crowdy is not synced to this branch's pin: close the editor and run scripts\sync-sdk.ps1 -Build" -ForegroundColor Yellow
    }
}
finally { Pop-Location }

Write-Host ""
Write-Host "OK" -ForegroundColor Green
Write-Host "  branch $Name, base $devCommit"
Write-Host ""
Write-Host "Before your first edit in the other developer's default area (Internal/Contracts.md C0.1), to a contract, or to"
Write-Host "anything they build on, stop: your developer flags it in Discord first, and you continue once they say it went out"
Write-Host "(AGENTS.md section 7). Not before the merge: before the first edit."
exit 0
