<#
.SYNOPSIS
    Works out which Unreal Engine install a script should build against.

.DESCRIPTION
    Dot-source this and call Resolve-EngineRoot. It answers without being told, so no script here
    has to hardcode a path and no caller has to know where the engine lives.

    The order is deliberate. The .uproject's EngineAssociation comes before "newest installed"
    because it is the only source that says which engine this project actually wants: picking the
    newest silently builds against 5.9 the day someone installs it, and the failure looks like a
    code fault.

      1. -EngineRoot                the caller knows better than any of this
      2. $env:UE_ROOT               a machine that wants one answer for every script
      3. EngineAssociation          the version (or source-build GUID) the .uproject declares
      4. newest registered install  a project with no association, or one naming a version that
                                    is not installed
      5. filesystem scan            an engine the registry and launcher never recorded

    Every candidate is checked for Engine\Build\BatchFiles\Build.bat before it is returned, so a
    stale registry entry pointing at a deleted install falls through to the next source instead of
    being handed back as an answer.

    Returns $null when nothing resolves. Callers report that themselves, because what they need
    from an engine root differs (Build.bat, UnrealEditor-Cmd.exe).
#>

# Plain .NET path calls, because Join-Path throws on a missing drive under ErrorActionPreference Stop.
function Test-EngineRoot {
    param([string] $Path)

    if (-not $Path) { return $false }
    try {
        $BuildBat = [System.IO.Path]::Combine($Path, "Engine", "Build", "BatchFiles", "Build.bat")
        return [System.IO.File]::Exists($BuildBat)
    } catch {
        return $false
    }
}

# Registered engines, newest first, from both the registry and the launcher manifest, which disagree.
function Get-RegisteredEngines {
    $Found = @{}

    try {
        foreach ($Key in Get-ChildItem "HKLM:\SOFTWARE\EpicGames\Unreal Engine" -ErrorAction Stop) {
            $Dir = (Get-ItemProperty $Key.PSPath -ErrorAction SilentlyContinue).InstalledDirectory
            if ($Dir -and -not $Found.ContainsKey($Key.PSChildName)) { $Found[$Key.PSChildName] = $Dir }
        }
    } catch {}

    $Manifest = "C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat"
    if (Test-Path $Manifest) {
        try {
            foreach ($App in (Get-Content $Manifest -Raw | ConvertFrom-Json).InstallationList) {
                if ($App.AppName -match '^UE_(\d+\.\d+)$' -and $App.InstallLocation) {
                    if (-not $Found.ContainsKey($Matches[1])) { $Found[$Matches[1]] = $App.InstallLocation }
                }
            }
        } catch {}
    }

    return $Found.GetEnumerator() |
        Where-Object { $_.Key -match '^(\d+)\.(\d+)$' } |
        ForEach-Object { [pscustomobject]@{ Version = [version]$_.Key; Path = $_.Value } } |
        Sort-Object Version -Descending
}

# A source build registers by GUID, not by version.
function Get-SourceBuildRoot {
    param([string] $Guid)

    try {
        $Builds = Get-ItemProperty "HKCU:\Software\Epic Games\Unreal Engine\Builds" -ErrorAction Stop
        $Match = $Builds.PSObject.Properties | Where-Object { $_.Name -eq $Guid } | Select-Object -First 1
        if ($Match) { return $Match.Value }
    } catch {}

    return $null
}

# Last resort: walk every fixed drive for an engine nothing recorded.
function Find-UnregisteredEngines {
    $Roots = foreach ($Drive in [System.IO.DriveInfo]::GetDrives()) {
        if ($Drive.DriveType -ne 'Fixed' -or -not $Drive.IsReady) { continue }
        Join-Path $Drive.Name "Epic Games"
        Join-Path $Drive.Name "Program Files\Epic Games"
    }

    $Candidates = foreach ($Root in ($Roots | Select-Object -Unique)) {
        if (-not (Test-Path $Root)) { continue }
        foreach ($Dir in (Get-ChildItem $Root -Directory -ErrorAction SilentlyContinue)) {
            if ($Dir.Name -match '^UE_(\d+)\.(\d+)') {
                [pscustomobject]@{
                    Version = [version]("{0}.{1}" -f $Matches[1], $Matches[2])
                    Path    = $Dir.FullName
                }
            }
        }
    }

    return $Candidates | Sort-Object Version -Descending
}

function Resolve-EngineRoot {
    param(
        [string] $Explicit,
        [string] $ProjectFile
    )

    if ($Explicit) {
        # Unvalidated on purpose, so a wrong explicit path fails loudly.
        return $Explicit
    }

    if ($env:UE_ROOT -and (Test-EngineRoot $env:UE_ROOT)) {
        Write-Verbose "Engine root from UE_ROOT: $env:UE_ROOT"
        return $env:UE_ROOT
    }

    $Registered = @(Get-RegisteredEngines)

    if ($ProjectFile -and (Test-Path $ProjectFile)) {
        $Association = $null
        try { $Association = (Get-Content $ProjectFile -Raw | ConvertFrom-Json).EngineAssociation } catch {}

        if ($Association) {
            if ($Association -match '^\d+\.\d+$') {
                $Wanted = $Registered | Where-Object { $_.Version -eq [version]$Association } | Select-Object -First 1
                if ($Wanted -and (Test-EngineRoot $Wanted.Path)) {
                    Write-Verbose "Engine root from EngineAssociation $Association : $($Wanted.Path)"
                    return $Wanted.Path
                }
            }
            else {
                $Source = Get-SourceBuildRoot $Association
                if (Test-EngineRoot $Source) {
                    Write-Verbose "Engine root from source build $Association : $Source"
                    return $Source
                }
            }
        }
    }

    foreach ($Engine in $Registered) {
        if (Test-EngineRoot $Engine.Path) {
            Write-Verbose "Engine root from newest registered install: $($Engine.Path)"
            return $Engine.Path
        }
    }

    foreach ($Engine in (Find-UnregisteredEngines)) {
        if (Test-EngineRoot $Engine.Path) {
            Write-Verbose "Engine root found on disk: $($Engine.Path)"
            return $Engine.Path
        }
    }

    return $null
}
