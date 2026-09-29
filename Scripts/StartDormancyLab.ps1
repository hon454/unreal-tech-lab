param(
    [string]$Name = ('resource-' + (Get-Date -Format 'yyyyMMdd-HHmmss')),
    [switch]$Automated, [switch]$HeadlessValidation, [switch]$Compare, [switch]$Listen,
    [ValidateRange(0,2)][int]$Policy = 0, [int]$Count = 1024, [int]$Percent = 1,
    [double]$Interval = 1, [int]$WarmSeconds = 5, [int]$StableSeconds = 15,
    [int]$ChangeSeconds = 15, [int]$Loss = 0, [int]$Lag = 0, [int]$Port = 17888,
    [ValidateRange(1,2)][int]$DriverClient = 1,
    [switch]$LateJoin, [switch]$ControlTest
)
$ErrorActionPreference = 'Stop'
if ($LateJoin -and ($Listen -or $DriverClient -ne 1)) { throw 'LateJoin requires Dedicated mode and DriverClient 1' }
$root = Split-Path $PSScriptRoot
$engine = 'G:/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe'
$folder = Join-Path $root "Saved/DormancyRuns/$Name"
if (Test-Path -LiteralPath $folder) { throw "Output exists: $folder" }
New-Item -ItemType Directory -Path $folder -Force | Out-Null
$project = '"' + (Join-Path $root 'TechLab.uproject') + '"'
$map = '/Game/Maps/Networking/L_Networking_Dormancy'
$trace = @('-trace=cpu,frame,bookmark,counters,net,region','-NetTrace=1','-statnamedevents')
$common = @($project,'-nosplash','-nosound','-NoLiveCoding','-NoVSync','-ExecCmds="t.MaxFPS 60,net.UseAdaptiveNetUpdateFrequency 0"')
$auto = @('-LabAuto',"-LabScenario=$Policy","-LabCount=$Count","-LabPercent=$Percent","-LabInterval=$Interval")
if ($Compare) { $auto += '-LabCompare' }
if ($ControlTest) { $auto += '-LabControlTest' }
if ($LateJoin) { $auto += @('-LabClients=1','-LabExitDelay=20') }
if ($Listen) { $auto += '-LabClients=1' }
$serverArgs = $common + @($map) + $trace + @("-port=$Port","-LabWarm=$WarmSeconds","-LabStable=$StableSeconds","-LabChange=$ChangeSeconds","-tracefile=$folder/server.utrace","-abslog=$folder/server.log")
if ($Listen) {
    $serverArgs = $common + @("${map}?listen",'-game','-windowed','-ResX=1200','-ResY=800','-WinX=40','-WinY=40') + $serverArgs[($common.Count+1)..($serverArgs.Count-1)]
    if ($HeadlessValidation) { $serverArgs += '-nullrhi' }
    if ($Automated) { $serverArgs += $auto + '-LabDrive' }
} else { $serverArgs += @('-server','-nullrhi') }
if ($Automated) { $serverArgs += '-LabAutoExit' }
if ($LateJoin) { $serverArgs += '-LabExitDelay=30' }
if ($Loss -or $Lag) { $serverArgs += @("-PktLoss=$Loss","-PktLag=$Lag") }
$processes = @()
function Wait-Log([string]$Path,[string]$Pattern,[int]$Seconds=120) {
    $deadline=(Get-Date).AddSeconds($Seconds)
    while (-not (Test-Path -LiteralPath $Path) -or -not (Select-String -LiteralPath $Path -Pattern $Pattern -Quiet)) {
        if ($server.HasExited -or (Get-Date)-gt $deadline) { throw "Timeout: $Pattern ($Path)" }
        Start-Sleep -Milliseconds 500
    }
}
try {
    if ($Listen -and -not $HeadlessValidation) {
        $server=Start-Process $engine -ArgumentList $serverArgs -PassThru
    } else {
        $server=Start-Process $engine -ArgumentList $serverArgs -WindowStyle Hidden -PassThru
    }
    $processes += $server
    Wait-Log "$folder/server.log" 'GameNetDriver.*listening on port'
    $clientCount = if ($Listen) { 1 } else { 2 }
    foreach ($index in 0..($clientCount-1)) {
        if ($LateJoin -and $index -eq 1) { Wait-Log "$folder/server.log" 'LAB_EVENT.* Complete,' 180 }
        $label = if ($Listen -or $index -eq 1) { 'client2' } else { 'client1' }
        $clientArgs=$common + @("127.0.0.1:$Port",'-game',"-abslog=$folder/$label.log")
        if ($Automated) {
            $clientArgs += $auto
            if (-not $Listen -and ($index + 1) -eq $DriverClient) { $clientArgs += '-LabDrive' }
        }
        if ($HeadlessValidation) {
            $clientArgs += @('-nullrhi','-unattended')
            $client=Start-Process $engine -ArgumentList $clientArgs -WindowStyle Hidden -PassThru
        } else {
            # Visible clients are intentional: the experiment must be observable in the map.
            $x = if ($label -eq 'client1') { 40 } else { 1260 }
            $clientArgs += @('-windowed','-ResX=1200','-ResY=800',"-WinX=$x",'-WinY=40')
            $client=Start-Process $engine -ArgumentList $clientArgs -PassThru
        }
        $processes += $client
        if ($index -eq 0 -and -not $Listen) { Wait-Log "$folder/server.log" 'Join succeeded' }
    }
    $processes | Select-Object Id,ProcessName,StartTime | ConvertTo-Json | Set-Content "$folder/processes.json"
    @{
        schema='resource-multiroom-v3'; visual_clients=(-not $HeadlessValidation); listen=[bool]$Listen
        automated=[bool]$Automated; count=$Count; percent=$Percent; interval=$Interval
        policy=$Policy; compare=[bool]$Compare; loss=$Loss; lag=$Lag
        module_sha256=(Get-FileHash "$root/Binaries/Win64/UnrealEditor-TechLab.dll").Hash
        map_sha256=(Get-FileHash "$root/Content/Maps/Networking/L_Networking_Dormancy.umap").Hash
    } | ConvertTo-Json | Set-Content "$folder/execution.json"
    Write-Output "Session: $folder. F: attack, /: settings, run, stop and results. All clients can attack and control."
    if ($Automated) {
        $limit=(Get-Date).AddSeconds(240 + 3*($WarmSeconds+$StableSeconds+$ChangeSeconds))
        while (-not $server.HasExited) {
            if ((Get-Date)-gt $limit) { throw 'Experiment timed out' }
            Start-Sleep -Seconds 1
        }
        (Select-String -LiteralPath "$folder/server.log" -Pattern 'LAB_EVENT|LAB_REQUEST').Line | Set-Content "$folder/run-events.txt"
        if (-not (Select-String "$folder/server.log" -Pattern 'LAB_EVENT.* Complete,' -Quiet)) { throw 'No successful final-state verification' }
        if (Select-String "$folder/server.log" -Pattern 'LAB_EVENT.* Failed,' -Quiet) { throw 'A run failed verification' }
        if ($LateJoin -and -not (Select-String "$folder/server.log" -Pattern 'LateJoinMatch:' -Quiet)) { throw 'Late join failed' }
        Write-Output 'PASSED server-path verification (not an input/UI test).'
    }
} finally {
    if ($Automated -or $Error.Count -gt 0) {
        foreach ($process in $processes) {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id -ErrorAction SilentlyContinue }
        }
    }
}
