param(
    [string]$Target = 'TechLabEditor',
    [ValidateSet('Debug', 'DebugGame', 'Development', 'Shipping', 'Test')]
    [string]$Configuration = 'Development',
    [string]$Platform = 'Win64'
)
$ErrorActionPreference = 'Stop'

$root = Split-Path $PSScriptRoot
$project = Join-Path $root 'TechLab.uproject'
$association = (Get-Content -LiteralPath $project -Raw | ConvertFrom-Json).EngineAssociation

# Resolution order: UE_ROOT env var, installed build registry, source build registry, Epic Launcher manifest.
function Resolve-EngineRoot {
    if ($env:UE_ROOT) {
        return $env:UE_ROOT
    }

    $installed = Get-ItemProperty "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$association" -ErrorAction SilentlyContinue
    if ($installed.InstalledDirectory) {
        return $installed.InstalledDirectory
    }

    $builds = Get-ItemProperty 'HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds' -ErrorAction SilentlyContinue
    if ($builds -and $builds.$association) {
        return $builds.$association
    }

    $manifest = Join-Path $env:ProgramData 'Epic\UnrealEngineLauncher\LauncherInstalled.dat'
    if (Test-Path -LiteralPath $manifest) {
        $entry = (Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json).InstallationList |
            Where-Object { $_.AppName -eq "UE_$association" } |
            Select-Object -First 1
        if ($entry) {
            return $entry.InstallLocation
        }
    }

    throw "Engine '$association' not found. Set UE_ROOT to the engine root folder."
}

$engineRoot = Resolve-EngineRoot
$buildBat = Join-Path $engineRoot 'Engine\Build\BatchFiles\Build.bat'
if (-not (Test-Path -LiteralPath $buildBat)) {
    throw "Build.bat not found: $buildBat"
}

Write-Output "Engine: $engineRoot"
& $buildBat $Target $Platform $Configuration "-Project=$project" -WaitMutex -NoHotReloadFromIDE
exit $LASTEXITCODE
