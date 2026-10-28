<#
.SYNOPSIS
    Checks that a committed work branch is ready to land on dev: merges dev in, runs the checks, and prints a block for
    the as-built note.

.DESCRIPTION
    Run it on your work branch when the work is committed, and again just before landing. It merges dev into the branch
    first, so every conflict or break shows up on the branch and never on dev. It stops with one line at the first
    gate that fails:

      - The branch is a work branch cut from dev: not dev, test, prod or a promote/ branch.
      - The workspace has no pending files. Every uncommitted, new or deleted file is a question for your developer:
        commit it, keep it for later, or discard it. Nothing is silently left, committed or discarded.
      - No Unreal editor has this project open, because the merge rewrites files it holds.
      - dev merges in without a conflict, and dev did not move while it merged.
      - dv conflict-backups list is empty, and the SDK mount is synced to the pin with 0 local changes.

    It then reads the change list (dv diff --base dev --compare <branch>) and lists every problem in one pass:

      - Plugins/Crowdy changed on a branch that is not a repin/ branch.
      - A build input (Source, Config, Content text files, a Construct plugin, the .uproject, .uplugin or Build.cs)
        names Plugins/Crowdy or Plugins/CrowdySDK. The SDK sits at a different path on the GitHub mirror.
      - A parallel-path marker in changed code: CrowdyCppBridge in a Build.cs, a crowdy/ include, an include through a
        Private path, FHttpModule or IWebSocket.
      - The pre-commit hook refuses a changed file. This catches commits that skipped the hook.

    Then scripts\check-sdk-pin.ps1 runs, and the editor and game targets build when the change list needs it: anything
    under Source/ or Config/ (the project's or a plugin's), a .uproject or .uplugin, or the SDK pin. Content and text
    alone build nothing. A build counts only with real Compile or Link lines or an up-to-date report.
    Warnings never fail: a missing Changes/ fragment, as-built note or plan update, an AI harness change, a deploy
    record.

    On PASS it prints a block to paste into the as-built note. It changes only the branch it is run on: it merges dev
    in. It never commits, and it never merges into dev. Landing is a separate, deliberate dv merge <branch> --into dev.

.PARAMETER Build
    Build TheConstructEditor even when the change list does not need it.

.PARAMETER Game
    Build TheConstructEditor and the TheConstruct game target even when the change list does not need them.

.PARAMETER EngineRoot
    Unreal Engine install root, the folder containing Engine\. Omit it to use $env:UE_ROOT, then the engine the
    .uproject names, then the newest install found.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\check-branch.ps1

.NOTES
    Run from PowerShell, never from Git Bash: bash mangles the spaced engine path and the build step silently no-ops.
    Exits non-zero on any failure, so it is usable as a gate.
#>

[CmdletBinding()]
param(
    [switch] $Build,
    [switch] $Game,
    [string] $EngineRoot
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "Resolve-EngineRoot.ps1")

$RepoRoot    = Split-Path -Parent $PSScriptRoot
$ProjectFile = Join-Path $RepoRoot "TheConstruct.uproject"
$HookFile    = Join-Path $RepoRoot ".dvhooks\pre-commit.ps1"
$PinScript   = Join-Path $PSScriptRoot "check-sdk-pin.ps1"

$textExt = '\.(cs|h|hpp|cpp|inl|ini|uplugin|uproject|json|py)$'
$codeExt = '\.(cs|h|hpp|cpp|inl)$'

$problems = @()
$warnings = @()
function Add-Problem([string]$Text) { $script:problems += $Text }
function Add-Warning([string]$Text) { $script:warnings += $Text }
function Write-Ok([string]$Text) { Write-Host "  ok   $Text" -ForegroundColor Green }

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

# Windows PowerShell 5.1 turns a native command's stderr into a terminating error under Stop.
function Invoke-Native([string]$File, [string[]]$Arguments) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try { $lines = @(& $File @Arguments 2>&1 | ForEach-Object { "$_" }) }
    finally { $ErrorActionPreference = $previous }
    return [pscustomobject]@{ Code = $LASTEXITCODE; Lines = $lines }
}

function Write-Pending([string]$Status) {
    $lines = @($Status -split "`r?`n" | Where-Object { $_ -match '^\s+\S' })
    $lines | Select-Object -First 20 | ForEach-Object { Write-Host "    $($_.Trim())" -ForegroundColor Yellow }
    if ($lines.Count -gt 20) { Write-Host "    ... and $($lines.Count - 20) more" -ForegroundColor Yellow }
}

function Get-DevCommit {
    $head = Get-FirstLine (Invoke-Dv show dev)
    $commit = [regex]::Match($head, '^commit (dv\.commit\.\d+)').Groups[1].Value
    if (-not $commit) { Fail "could not read dev's head from 'dv show dev': $head" }
    return $commit
}

# One entry per path; a rename gives its old path as a deletion and its new path as the rename.
function ConvertFrom-NameStatus([string]$Text) {
    $entries = @()
    foreach ($line in ($Text -split "`r?`n")) {
        if (-not $line.Trim()) { continue }
        $cols = $line -split "`t| -> "
        if ($cols.Count -lt 2 -or $cols[0] -notmatch '^[A-Z]\d*$') { Fail "could not read this line of dv diff --name-status: $line" }
        for ($i = 1; $i -lt $cols.Count; $i++) {
            $kind = if ($cols[0][0] -eq 'R' -and $i -eq 1) { 'D' } else { [string]$cols[0][0] }
            $path = $cols[$i].Trim().Trim('"') -replace '\\', '/'
            $entries += [pscustomobject]@{ Kind = $kind; Path = $path }
        }
    }
    return $entries
}

function Test-BuildInput([string]$Path) {
    if ($Path -match '^(Source|Config|Content)/') { return $true }
    if ($Path -match '^Plugins/(?!Crowdy(/|$))') { return $true }
    return $Path -match '\.(uproject|uplugin)$' -or $Path -match '\.Build\.cs$'
}

function Find-Lines([string[]]$Paths, [string]$Pattern, [bool]$CaseSensitive) {
    $hits = @()
    foreach ($p in $Paths) {
        $found = Select-String -LiteralPath (Join-Path $RepoRoot $p) -Pattern $Pattern -CaseSensitive:$CaseSensitive -ErrorAction SilentlyContinue
        foreach ($h in $found) { $hits += "${p}:$($h.LineNumber)" }
    }
    return $hits
}

function Invoke-TargetBuild([string]$BuildBat, [string]$Target) {
    Write-Host "Building $Target ..." -ForegroundColor Cyan
    $run = Invoke-Native $BuildBat @($Target, "Win64", "Development", "-Project=$ProjectFile", "-WaitMutex")
    $compiled = @($run.Lines | Select-String -Pattern "Compile \[x64\]").Count
    $linked   = @($run.Lines | Select-String -Pattern "Link \[x64\]").Count
    $upToDate = @($run.Lines | Select-String -Pattern "Target is up to date").Count -gt 0

    if ($run.Code -ne 0) {
        $run.Lines | Select-String -Pattern "error|Error:" | Select-Object -First 10 | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
        Fail "$Target build returned $($run.Code) after $compiled Compile and $linked Link line(s)"
    }
    if ($compiled -eq 0 -and $linked -eq 0 -and -not $upToDate) {
        Fail "$Target build printed no Compile or Link lines and did not report the target up to date, so nothing was verified"
    }
    if ($compiled -eq 0 -and $linked -eq 0) { return "$Target up to date, nothing rebuilt" }
    return "$Target $compiled Compile, $linked Link"
}

Write-Host "Branch check" -ForegroundColor Cyan
Write-Host "  repo: $RepoRoot"
Write-Host ""

Push-Location $RepoRoot
try {
    $branch = Get-FirstLine (Invoke-Dv branch-name)
    if ($branch -in "dev", "test", "prod") { Fail "'$branch' is a tier; run this on your work branch (AGENTS.md 4.1)" }
    if ($branch -match '^promote/') { Fail "'$branch' is a promotion: it follows the repo owner's promotion steps (AGENTS.md 4.5), and merging dev into it would promote dev" }
    Write-Ok "branch $branch"

    $status = Invoke-Dv status --no-limit
    if ($status -notmatch "Your workspace has no changes") {
        Write-Host "Pending files:" -ForegroundColor Yellow
        Write-Pending $status
        Fail "ask your developer about each pending file: commit it, keep it for later (dv shelf create <name> <paths>), or discard it; nothing lands until the workspace is clean (AGENTS.md 4.2)"
    }
    Write-Ok "no pending files"

    $editors = Get-CimInstance Win32_Process -Filter "Name LIKE 'UnrealEditor%'" -ErrorAction SilentlyContinue |
        Where-Object { -not $_.CommandLine -or ($_.CommandLine -replace '/', '\').IndexOf($RepoRoot, [System.StringComparison]::OrdinalIgnoreCase) -ge 0 }
    if ($editors) { Fail "close the editor that has this project open first: the merge rewrites files it holds (AGENTS.md 4.2)" }

    $conflicted = "dev merge conflicted: stop, resolve it on this branch with your developer (AGENTS.md 4.2), then re-run"
    $devCommit = Get-DevCommit
    $openBefore = (Invoke-Dv merge -l).Trim()
    Write-Host "Merging dev ($devCommit) into $branch ..." -ForegroundColor Cyan
    $merge = Invoke-Native "dv" @("merge", "dev")
    $mergeOut = $merge.Lines -join "`n"
    $merge.Lines | Where-Object { $_.Trim() } | Select-Object -Last 3 | ForEach-Object { Write-Host "  $_" }

    $reportsConflict = $mergeOut -match '(?i)conflict|did not complete|open merge' -and $mergeOut -notmatch '(?i)\b(no|0|zero|without) conflicts?\b|\bconflicts?:?\s*(0|none)\b'
    $alreadyMerged = $mergeOut -match '(?i)already|up.to.date|nothing to merge|no changes'
    if ($reportsConflict -or ($merge.Code -ne 0 -and -not $alreadyMerged)) { Fail $conflicted }

    # An open merge is repo-wide, so only a change in the list counts as this merge's.
    if ((Invoke-Dv merge -l).Trim() -ne $openBefore) { Fail $conflicted }

    $afterStatus = Invoke-Dv status --no-limit
    if ($afterStatus -notmatch "Your workspace has no changes") {
        Write-Pending $afterStatus
        Fail $conflicted
    }
    if ((Get-FirstLine (Invoke-Dv branch-name)) -ne $branch) { Fail "the workspace left '$branch' during the merge" }
    $devAfter = Get-DevCommit
    if ($devAfter -ne $devCommit) { Fail "dev moved from $devCommit to $devAfter while it merged; re-run" }
    $branchTip = Get-FirstLine (Invoke-Dv status --commit-id-only)
    Write-Ok "dev merged at $devCommit, branch now at $branchTip"

    $backups = Invoke-Dv conflict-backups list
    if ($backups -notmatch "No conflict backup files found") {
        $backups -split "`r?`n" | Where-Object { $_.Trim() } | ForEach-Object { Write-Host "    $($_.Trim())" -ForegroundColor Yellow }
        Fail "dv conflict-backups lists a file: Diversion moved a local edit into a .dv-conflict copy; stop and tell your developer (AGENTS.md 4.2)"
    }
    $mountRow = (Invoke-Dv submodule status) -split "`r?`n" | Where-Object { $_ -match '^\s*Plugins/Crowdy\s' } | Select-Object -First 1
    $cols = if ($mountRow) { @($mountRow.Trim() -split '\s+') } else { @() }
    if ($cols.Count -lt 5 -or $cols[3] -ne "synced" -or $cols[4] -ne "0") { Fail "close the editor and run scripts\sync-sdk.ps1 -Build, then re-run" }
    Write-Ok "no conflict backups, Plugins/Crowdy synced"

    $diff = Invoke-Dv diff --base dev --compare $branch --name-status
    if ($diff -match "No changes detected") { Fail "the branch has no changes against dev, so there is nothing to land" }
    $entries = @(ConvertFrom-NameStatus $diff)

    $parents = @{}
    foreach ($e in $entries) {
        $parts = $e.Path -split '/'
        for ($i = 1; $i -lt $parts.Count; $i++) { $parents[($parts[0..($i - 1)] -join '/')] = $true }
    }
    $files = @($entries | Where-Object {
        $_.Path -eq 'Plugins/Crowdy' -or (-not $parents.ContainsKey($_.Path) -and -not (Test-Path -LiteralPath (Join-Path $RepoRoot $_.Path) -PathType Container))
    })
    $present = @($files | Where-Object { $_.Kind -ne 'D' -and (Test-Path -LiteralPath (Join-Path $RepoRoot $_.Path) -PathType Leaf) } | ForEach-Object { $_.Path })
    Write-Ok "$($files.Count) changed file(s) against dev"
    foreach ($f in $files) { Write-Host ("       {0}  {1}" -f $f.Kind, $f.Path) -ForegroundColor DarkGray }

    if ($branch -cnotmatch '^repin/') {
        foreach ($e in @($entries | Where-Object { $_.Path -match '^Plugins/Crowdy(/|$)' })) {
            Add-Problem "$($e.Path) is changed on '$branch'; the SDK pin moves only on a repin/ branch, and nothing inside the mount is ever edited (AGENTS.md 5.1)"
        }
    }

    $inputs = @($present | Where-Object { (Test-BuildInput $_) -and $_ -match $textExt })
    foreach ($hit in @(Find-Lines $inputs 'Plugins[\\/]+Crowdy' $false)) {
        Add-Problem "$hit names the SDK's path; refer to the plugin and its modules by name (AGENTS.md 5.2)"
    }

    $code = @($inputs | Where-Object { $_ -match $codeExt })
    $markers = @(
        @{ What = 'CrowdyCppBridge in a Build.cs'; Paths = @($code | Where-Object { $_ -match '\.Build\.cs$' }); Pattern = 'CrowdyCppBridge'; Case = $false }
        @{ What = 'a crowdy/ include'; Paths = $code; Pattern = '#\s*include\s*[<"]crowdy/'; Case = $true }
        @{ What = 'an include through a Private path'; Paths = $code; Pattern = '#\s*include\s*[<"][^>"]*[/\\]Private[/\\]'; Case = $false }
        @{ What = 'FHttpModule or IWebSocket'; Paths = $code; Pattern = '\b(FHttpModule|IWebSocket)\b'; Case = $true }
    )
    foreach ($m in $markers) {
        foreach ($hit in @(Find-Lines $m.Paths $m.Pattern $m.Case)) {
            Add-Problem "$hit has $($m.What), a parallel path; use only the exposed SDK surface (AGENTS.md section 1)"
        }
    }

    $hookNotes = @()
    if (-not (Test-Path $HookFile)) {
        Add-Warning ".dvhooks\pre-commit.ps1 is missing in this tree, so the commit rules were not re-checked"
    }
    elseif ($present.Count -gt 0) {
        $env:DV_BRANCH = $branch
        $env:DV_REPO_ROOT = $RepoRoot
        $env:DV_COMMIT_MESSAGE = ""
        try {
            for ($i = 0; $i -lt $present.Count; $i += 50) {
                $chunk = @($present[$i..([Math]::Min($i + 49, $present.Count - 1))])
                $run = Invoke-Native "powershell" (@("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $HookFile) + $chunk)
                $lines = @($run.Lines | Where-Object { $_.Trim() })
                if ($run.Code -eq 0) { $hookNotes += $lines; continue }

                $hookNotes += @($lines | Where-Object { $_ -match '^pre-commit: note:' })
                $refusals = @($lines | Where-Object { $_ -notmatch '^pre-commit: note:' })
                if ($refusals.Count -eq 0) { Add-Problem "commit hook exited $($run.Code) without saying why" }
                $refusals | ForEach-Object { Add-Problem "commit hook: $($_ -replace '^pre-commit:\s*', '')" }
            }
        }
        finally {
            Remove-Item Env:DV_BRANCH, Env:DV_REPO_ROOT, Env:DV_COMMIT_MESSAGE -ErrorAction SilentlyContinue
        }
    }

    if ($problems.Count -gt 0) {
        Write-Host ""
        Write-Host "Problems:" -ForegroundColor Red
        $problems | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
        Fail "$($problems.Count) problem(s) in the change list; fix them on this branch, commit, and re-run"
    }
    Write-Ok "change list: SDK paths, parallel-path markers and the commit hook are clean"
    foreach ($note in $hookNotes) { Write-Host "       $note" -ForegroundColor DarkGray }

    $paths = @($entries | ForEach-Object { $_.Path })
    $fragment = "Changes/$($branch -replace '/', '-').md"
    $internalOnly = $branch -match '^docs/' -and -not ($paths | Where-Object { $_ -notmatch '^Internal/' -or $_ -match '^Internal/Harness/' })
    $outsideInternal = [bool]($paths | Where-Object { $_ -notmatch '^Internal/' })
    if ($outsideInternal -and $paths -notcontains $fragment) { Add-Warning "no $fragment in the change list (needed if the change is user-visible or moved the pin)" }
    if (-not $internalOnly -and -not ($paths | Where-Object { $_ -match '^Internal/As-Built/.' })) { Add-Warning "no file under Internal/As-Built/ changed (record the verification in the as-built note)" }
    if (-not $internalOnly -and $paths -notcontains 'Internal/Plan.md') { Add-Warning "Internal/Plan.md not changed (update the layer status row if this branch starts, finishes or defers a layer)" }
    if ($paths | Where-Object { $_ -match '^(AGENTS\.md|CLAUDE\.md|GEMINI\.md|\.claude/settings\.json|\.codex/|\.dvhooks/|Internal/Harness/)' }) {
        Add-Warning "AI harness change: both developers say yes in Discord before it merges (AGENTS.md 4.4)"
    }
    if ($paths | Where-Object { $_ -match '^Server/.+/revisions\.json$' }) {
        Add-Warning "a deploy record: note it under Deploys in the as-built note (AGENTS.md 4.11)"
    }

    $pin = Invoke-Native "powershell" @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $PinScript)
    if ($pin.Code -ne 0) {
        $pin.Lines | Where-Object { $_.Trim() } | ForEach-Object { Write-Host "    $_" -ForegroundColor Yellow }
        Fail "scripts\check-sdk-pin.ps1 failed; fix what it lists, then re-run"
    }
    Write-Ok "check-sdk-pin passes"

    $pinMoved = [bool]($paths | Where-Object { $_ -match '^Plugins/Crowdy(/|$)' })
    $needsBuild = $pinMoved -or [bool]($paths | Where-Object { $_ -match '^(Source|Config|Plugins/[^/]+/(Source|Config))/|\.(uproject|uplugin)$' })
    $buildEditor = $Build -or $Game -or $needsBuild
    $buildGame = $Game -or $needsBuild

    $buildResults = @()
    if ($buildEditor) {
        $EngineRoot = Resolve-EngineRoot -Explicit $EngineRoot -ProjectFile $ProjectFile
        $BuildBat = if ($EngineRoot) { Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat" } else { $null }
        if (-not $BuildBat -or -not (Test-Path $BuildBat)) {
            Fail "No Unreal Engine build script found$(if ($EngineRoot) { " under '$EngineRoot'" }). Pass -EngineRoot or set UE_ROOT."
        }
        $buildResults += Invoke-TargetBuild $BuildBat "TheConstructEditor"
        if ($buildGame) { $buildResults += Invoke-TargetBuild $BuildBat "TheConstruct" }
        $buildResults | ForEach-Object { Write-Ok $_ }
    }
}
finally { Pop-Location }

Write-Host ""
Write-Host "PASS" -ForegroundColor Green
if ($warnings.Count -gt 0) {
    Write-Host ""
    Write-Host "Warnings:" -ForegroundColor Yellow
    $warnings | ForEach-Object { Write-Host "  $_" -ForegroundColor Yellow }
}

Write-Host ""
if ($internalOnly) { Write-Host "Report to your developer:" }
else { Write-Host "Paste into the as-built note, under Verification:" }
Write-Host "  check-branch $(Get-Date -Format 'yyyy-MM-dd'): PASS"
Write-Host "  branch:         $branch"
Write-Host "  dev merged at:  $devCommit"
Write-Host "  changed files:  $($files.Count)"
Write-Host "  warnings:       $(if ($warnings.Count -gt 0) { $warnings.Count } else { 'none' })"
Write-Host "  build:          $(if ($buildResults.Count -gt 0) { $buildResults -join '; ' } else { 'not needed (no Source, Config, .uproject, .uplugin or pin change)' })"
Write-Host "  pin moved:      $(if ($pinMoved) { 'yes' } else { 'no' })"
Write-Host "  SDK surface:    <fill in after the AI review's lens 6: only exposed symbols used, or what was not; gaps filed: ids or none>"
Write-Host ""
Write-Host "Before dv merge $branch --into dev, dv show dev must still name $devCommit; if dev moved, run this again."
Write-Host "That merge is an ask line: an agent runs it only on a yes its developer gives after seeing this report and the AI review; a 'land it' given before the work is not that yes. If the guard blocks it or says permission was not granted, the agent stops, does not retry, and gives its developer these commands:"
Write-Host "  dv show dev                        (the first line must name $devCommit; if not, re-run scripts\check-branch.ps1 on $branch)"
Write-Host "  close the Unreal editor"
Write-Host "  dv merge $branch --into dev"
Write-Host "  dv checkout dev"
Write-Host "  dv update"
if ($internalOnly) { Write-Host "Next: the AI review (Internal/Harness/Review.md). A docs/ branch of Internal/ text needs no as-built note: report the review to your developer." }
else { Write-Host "Next: the AI review (Internal/Harness/Review.md), then record it in the as-built note." }
exit 0
