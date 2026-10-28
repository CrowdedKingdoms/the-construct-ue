<#
.SYNOPSIS
    The Diversion pre-commit hook: refuses the commit mistakes that AGENTS.md forbids and a script can see.

.DESCRIPTION
    dv commit runs this file directly. Diversion passes the committed file paths as arguments and sets DV_BRANCH,
    DV_COMMIT_MESSAGE and DV_REPO_ROOT. A long list may be split into chunks that run in parallel, so this keeps no
    state and never calls dv. It is a .ps1, not a .cmd, because cmd.exe refuses a file list over 8,191 characters.

    It refuses the commit (exit 1, one "pre-commit: ..." line per problem) when:
      - the branch is dev, test or prod, or its name is not one AGENTS.md allows
      - the message carries AI attribution (a Co-Authored-By line, "Generated with <agent>", the robot emoji) or an em-dash
      - the commit adds or changes a .gitignore or a .gitmodules (deleting one is fine)
      - a committed text file, or a file with no extension, contains an em-dash or a private key block
      - AGENTS.md is committed and is over 32,000 bytes
      - AGENTS.md, .claude/settings.json or .codex/rules/dv.rules is committed and the three no longer agree: the
        guarded dv command list in AGENTS.md section 6 is the source, the other two must carry exactly its entries

    It only prints a "pre-commit: note:" line, and leaves the exit code alone, for a path that is part of the AI harness
    or for a change to Plugins/Crowdy on a branch that is not repin/ or promote/.

    This is a net, not a guard. dv commit --no-verify skips it, commits made in the web app or through the API bypass
    it, and a branch has it only once the branch contains .dvhooks.

.EXAMPLE
    $env:DV_BRANCH = "feature/hud"; $env:DV_COMMIT_MESSAGE = "Add HUD"; $env:DV_REPO_ROOT = (Get-Location).Path
    powershell -NoProfile -ExecutionPolicy Bypass -File .dvhooks\pre-commit.ps1 Source/TheConstruct/Hud.cpp
#>

$branch = $env:DV_BRANCH
$message = $env:DV_COMMIT_MESSAGE
$root = $env:DV_REPO_ROOT
if (-not $root) { $root = (Get-Location).Path }
$rootNorm = ($root -replace '\\', '/').TrimEnd('/')

$emDash = [string][char]0x2014
$robot = [char]::ConvertFromUtf32(0x1F916)
$aiGenerated = '(?i)generated (with|by) .{0,20}(claude|codex|gemini|grok|copilot|cursor|chatgpt|openai)'
$maxBytes = 2MB
$agentsCap = 32000
$refused = 0

$branchShapes = @(
    '^(feature|fix|chore|docs)/[a-z0-9]+(-[a-z0-9]+){0,3}$'
    '^chore/release-(starter|construct)-[0-9]+\.[0-9]+\.[0-9]+(-(dev|test)\.[0-9]+)?$'
    '^repin/sdk-(dev|test|prod)-[0-9]+$'
    '^repin/sdk-check-[a-z0-9]+(-[a-z0-9]+){0,3}$'
    '^promote/(dev-to-test|test-to-prod)-[0-9]{8}$'
)
$textExts = @('md', 'txt', 'cs', 'h', 'hpp', 'cpp', 'inl', 'ini', 'json', 'ps1', 'cmd', 'bat', 'py', 'uplugin', 'uproject', 'rs', 'toml', 'rules', 'dvignore', 'yml', 'yaml', 'xml', 'js', 'ts', 'sh')
$harnessExact = @('agents.md', 'claude.md', 'gemini.md', '.claude/settings.json')
$harnessPrefixes = @('.codex/', '.dvhooks/', 'internal/harness/')
$agentsRel = 'AGENTS.md'
$claudeRel = '.claude/settings.json'
$codexRel = '.codex/rules/dv.rules'
$guardFiles = @($agentsRel.ToLowerInvariant(), $claudeRel, $codexRel)
$guardHeading = '### Guarded dv commands'
$guardTouched = $false

function Add-Refusal([string]$Problem, [string]$Section) {
    $script:refused++
    Write-Host "pre-commit: $Problem (AGENTS.md $Section)"
}

function Add-Note([string]$Text) {
    Write-Host "pre-commit: note: $Text"
}

function Read-SmallText([string]$Rel) {
    try {
        $path = if ([System.IO.Path]::IsPathRooted($Rel)) { $Rel } else { "$rootNorm/$Rel" }
        $info = [System.IO.FileInfo]$path
        if (-not $info.Exists -or $info.Length -gt $maxBytes) { return $null }
        return [System.IO.File]::ReadAllText($info.FullName)
    }
    catch {
        Add-Note "could not read $Rel, so its content was not checked"
        return $null
    }
}

function Test-HarnessPath([string]$Lower) {
    if ($harnessExact -contains $Lower) { return $true }
    foreach ($prefix in $harnessPrefixes) {
        if ($Lower.StartsWith($prefix)) { return $true }
    }
    return $false
}

function Format-Pattern([string[]]$Tokens) {
    return '["' + ($Tokens -join '", "') + '"]'
}

function Get-GuardedRows([string]$AgentsText) {
    $state = 'seek'
    $rows = New-Object System.Collections.Generic.List[string]
    foreach ($line in ($AgentsText -split "\r?\n")) {
        if ($state -eq 'seek' -and $line -eq $guardHeading) { $state = 'fence'; continue }
        if ($state -eq 'fence' -and $line -eq '```text') { $state = 'rows'; continue }
        if ($state -ne 'rows') { continue }
        if ($line -eq '```') { $state = 'done'; break }
        if ($line.Trim()) { $rows.Add($line.TrimEnd()) }
    }
    $problem = ''
    if ($state -eq 'seek') { $problem = "AGENTS.md has no '$guardHeading' heading" }
    elseif ($state -eq 'fence') { $problem = "AGENTS.md has no text block under '$guardHeading'" }
    elseif ($state -eq 'rows') { $problem = "the text block under '$guardHeading' is not closed" }
    elseif ($rows.Count -eq 0) { $problem = "the text block under '$guardHeading' has no rows" }
    foreach ($row in $rows) {
        if ($problem) { break }
        if ($row -notmatch '^(deny|ask) \S') { $problem = "the guarded dv command block has a row that is not 'deny <pattern>' or 'ask <pattern>': $row" }
    }
    return [pscustomobject]@{ Rows = $rows.ToArray(); Problem = $problem }
}

function Get-ExpectedEntries([string[]]$Rows) {
    $claude = New-Object System.Collections.Generic.List[string]
    $codex = New-Object System.Collections.Generic.List[string]
    foreach ($row in $Rows) {
        $decision, $pattern = $row -split ' ', 2
        $claude.Add("$decision Bash($pattern)")
        $claude.Add("$decision PowerShell($pattern)")
        $tokens = @($pattern -split ' ' | ForEach-Object { $_ -replace '\*', '' } | Where-Object { $_ })
        $verdict = if ($decision -eq 'deny') { 'forbidden' } else { 'prompt' }
        $codex.Add("$verdict " + (Format-Pattern $tokens))
    }
    return [pscustomobject]@{ Claude = $claude.ToArray(); Codex = $codex.ToArray() }
}

function Get-ClaudeEntries([string]$Json) {
    $entries = New-Object System.Collections.Generic.List[string]
    try { $doc = $Json | ConvertFrom-Json -ErrorAction Stop }
    catch {
        Add-Refusal "$claudeRel is not valid JSON, so it cannot be checked against the guarded dv command list" "6"
        return $null
    }
    foreach ($decision in 'deny', 'ask') {
        foreach ($entry in @($doc.permissions.$decision)) {
            if ($entry -isnot [string]) { continue }
            if ($entry.StartsWith('Bash(dv ', [System.StringComparison]::Ordinal) -or $entry.StartsWith('PowerShell(dv ', [System.StringComparison]::Ordinal)) {
                $entries.Add("$decision $entry")
            }
        }
    }
    return , $entries.ToArray()
}

function Get-CodexEntries([string]$Text) {
    $entries = New-Object System.Collections.Generic.List[string]
    $lineNo = 0
    foreach ($line in ($Text -split "\r?\n")) {
        $lineNo++
        $trim = $line.Trim()
        if (-not $trim -or $trim.StartsWith('#')) { continue }
        if ($trim -notmatch '^prefix_rule\(\s*pattern\s*=\s*\[(.*?)\]\s*,\s*decision\s*=\s*"(\w+)"') {
            Add-Refusal "$codexRel line $lineNo is not a one-line prefix_rule(pattern = [...], decision = ...)" "6"
            continue
        }
        $patternText = $Matches[1]
        $verdict = $Matches[2]
        $tokens = @([regex]::Matches($patternText, '"([^"]*)"') | ForEach-Object { $_.Groups[1].Value })
        $entries.Add("$verdict " + (Format-Pattern $tokens))
    }
    return , $entries.ToArray()
}

function Compare-Entries([string[]]$Expected, [string[]]$Actual, [string]$Rel) {
    $counts = New-Object 'System.Collections.Generic.Dictionary[string,int]'
    foreach ($entry in $Expected) {
        $n = 0
        [void]$counts.TryGetValue($entry, [ref]$n)
        $counts[$entry] = $n + 1
    }
    foreach ($entry in $Actual) {
        $n = 0
        if ($counts.TryGetValue($entry, [ref]$n) -and $n -gt 0) { $counts[$entry] = $n - 1; continue }
        Add-Refusal "$Rel has an entry that is not in the guarded dv command list: $entry" "6"
    }
    foreach ($entry in $Expected) {
        if ($counts[$entry] -le 0) { continue }
        $counts[$entry] = $counts[$entry] - 1
        Add-Refusal "$Rel is missing an entry of the guarded dv command list: $entry" "6"
    }
}

function Read-GuardFile([string]$Rel) {
    $text = $null
    if ([System.IO.File]::Exists("$rootNorm/$Rel")) { $text = Read-SmallText $Rel }
    if ($null -eq $text) { Add-Refusal "$Rel is missing or unreadable, so it cannot be checked against the guarded dv command list" "6" }
    return $text
}

function Test-GuardDrift {
    $agents = Read-GuardFile $agentsRel
    $claude = Read-GuardFile $claudeRel
    $codex = Read-GuardFile $codexRel
    if ($null -eq $agents -or $null -eq $claude -or $null -eq $codex) { return }

    $guard = Get-GuardedRows $agents
    if ($guard.Problem) { Add-Refusal $guard.Problem "6"; return }

    $expected = Get-ExpectedEntries $guard.Rows
    $claudeEntries = Get-ClaudeEntries $claude
    if ($null -ne $claudeEntries) { Compare-Entries $expected.Claude $claudeEntries $claudeRel }
    $codexEntries = Get-CodexEntries $codex
    Compare-Entries $expected.Codex $codexEntries $codexRel
}

if (-not $branch) {
    Add-Note "DV_BRANCH is empty, so the branch rules were skipped"
}
elseif ($branch -in 'dev', 'test', 'prod') {
    Add-Refusal "branch $branch is a tier; cut a work branch from it" "4.1"
}
else {
    $shapeOk = $false
    foreach ($shape in $branchShapes) {
        if ($branch -cmatch $shape) { $shapeOk = $true; break }
    }
    if (-not $shapeOk) { Add-Refusal "branch $branch is not an allowed work branch name" "4.2" }
}

if ($message) {
    if ($message -match '(?i)co-authored-by:' -or $message -match $aiGenerated -or $message.Contains($robot)) {
        Add-Refusal "commit message carries AI attribution" "4.9"
    }
    if ($message.Contains($emDash)) { Add-Refusal "commit message contains an em-dash" "4.9" }
}

function ConvertTo-RepoPath([string]$Path) {
    $rel = $Path.Trim().Trim('"') -replace '\\', '/'
    if ($rel.StartsWith($rootNorm + '/', [System.StringComparison]::OrdinalIgnoreCase)) { $rel = $rel.Substring($rootNorm.Length + 1) }
    while ($rel.StartsWith('./')) { $rel = $rel.Substring(2) }
    return $rel.TrimEnd('/')
}

# A committed folder arrives as one path, so its files are listed here; build output the .dvignore skips is left out.
$ignoredDirs = '^((binaries|intermediate|saved|deriveddatacache|\.vs|\.idea)/|plugins/[^/]+/(binaries|intermediate)/|server/[^/]+/target/|plugins/crowdy/)|/node_modules/'
$paths = New-Object System.Collections.Generic.List[string]
foreach ($arg in $args) {
    $rel = ConvertTo-RepoPath ([string]$arg)
    if (-not $rel) { continue }
    $dir = if ([System.IO.Path]::IsPathRooted($rel)) { $rel } else { "$rootNorm/$rel" }
    if ($rel -match '^(?i)plugins/crowdy(/|$)' -or -not [System.IO.Directory]::Exists($dir)) { $paths.Add($rel); continue }
    foreach ($file in [System.IO.Directory]::EnumerateFiles($dir, '*', [System.IO.SearchOption]::AllDirectories)) {
        $sub = ConvertTo-RepoPath $file
        if ($sub.ToLowerInvariant() -notmatch $ignoredDirs) { $paths.Add($sub) }
    }
}

foreach ($rel in $paths) {
    $lower = $rel.ToLowerInvariant()
    $leaf = $lower.Substring($lower.LastIndexOf('/') + 1)

    $full = if ([System.IO.Path]::IsPathRooted($rel)) { $rel } else { "$rootNorm/$rel" }
    $exists = [System.IO.File]::Exists($full)
    if ($exists -and $leaf -eq '.gitignore') { Add-Refusal "$rel is a .gitignore; ignore rules go in .dvignore" "6" }
    if ($exists -and $leaf -eq '.gitmodules') { Add-Refusal "$rel is a .gitmodules; Diversion mounts the SDK as a dv submodule" "6" }

    if (Test-HarnessPath $lower) {
        Add-Note "$rel is part of the AI harness; both developers say yes before it merges (AGENTS.md 4.4)"
    }

    if ($guardFiles -contains $lower) { $guardTouched = $true }
    if ($exists -and $lower -eq 'agents.md') {
        $agentsSize = ([System.IO.FileInfo]$full).Length
        if ($agentsSize -gt $agentsCap) { Add-Refusal "AGENTS.md is $agentsSize bytes; the cap is 32,000 so every agent loads all of it" "header" }
    }

    if ($lower -eq 'plugins/crowdy' -or $lower.StartsWith('plugins/crowdy/')) {
        if ($branch -notmatch '^(repin|promote)/') {
            Add-Note "$rel is the SDK mount; its pin moves only on a repin/ or promote/ branch (AGENTS.md 5.1)"
        }
        continue
    }

    if (-not $exists) { continue }
    $hasExt = $leaf -match '\.([^./]+)$'
    if ($hasExt -and $textExts -notcontains $Matches[1]) { continue }

    $text = Read-SmallText $rel
    if ($null -eq $text) { continue }

    if ($text.Contains($emDash)) { Add-Refusal "$rel contains an em-dash" "4.9" }
    elseif ($text.Contains([string][char]0xFFFD) -and ([System.IO.File]::ReadAllBytes($full) -contains [byte]0x97)) {
        Add-Refusal "$rel contains an em-dash saved as Windows-1252 (byte 0x97); save it as UTF-8 without the dash" "4.9"
    }
    if ($text -match '-----BEGIN [A-Z ]*PRIVATE KEY-----') { Add-Refusal "$rel contains a private key" "6" }
}

if ($guardTouched) { Test-GuardDrift }

if ($refused -gt 0) { exit 1 }
exit 0
