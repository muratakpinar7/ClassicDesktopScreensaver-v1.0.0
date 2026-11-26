[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('A', 'B', 'C')]
    [string]$Test,

    [ValidatePattern('^$|^[A-Za-z0-9_-]{1,24}$')]
    [string]$RunId = '',

    [Parameter(Mandatory = $true)]
    [ValidateSet('Start', 'End')]
    [string]$Phase,

    [string]$ExecutablePath
)

$ErrorActionPreference = 'Stop'
$outputDirectory = Join-Path ([System.IO.Path]::GetTempPath()) 'ClassicDesktopScreensaver-SleepComparison'
$testStem = "Test-$Test"
if (-not [string]::IsNullOrWhiteSpace($RunId)) {
    $testStem += "-$RunId"
}
$recordPath = Join-Path $outputDirectory "$testStem.json"
$powercfgPath = Join-Path $env:WINDIR 'System32\powercfg.exe'

function Get-ReadOnlyContext {
    $computer = Get-CimInstance -ClassName Win32_ComputerSystem
    $operatingSystem = Get-CimInstance -ClassName Win32_OperatingSystem

    [ordered]@{
        Manufacturer = $computer.Manufacturer
        Model = $computer.Model
        WindowsCaption = $operatingSystem.Caption
        WindowsVersion = $operatingSystem.Version
        WindowsBuild = $operatingSystem.BuildNumber
        ActivePowerScheme = (& $powercfgPath /getactivescheme 2>&1 | Out-String).Trim()
        AvailablePowerStates = (& $powercfgPath /a 2>&1 | Out-String).Trim()
    }
}

if (-not (Test-Path -LiteralPath $powercfgPath -PathType Leaf)) {
    throw "powercfg.exe was not found at $powercfgPath"
}

if ($Phase -eq 'Start') {
    if (Test-Path -LiteralPath $recordPath) {
        throw "A record for Test $Test already exists at $recordPath. Move it aside before repeating this test."
    }

    if ($Test -in @('B', 'C') -and [string]::IsNullOrWhiteSpace($ExecutablePath)) {
        throw "Test $Test requires -ExecutablePath so the exact application binary can be recorded."
    }

    if ($Test -eq 'A' -and -not [string]::IsNullOrWhiteSpace($ExecutablePath)) {
        throw 'Test A is the Windows Start menu test and does not take an application executable.'
    }

    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

    $executable = $null
    if (-not [string]::IsNullOrWhiteSpace($ExecutablePath)) {
        $resolvedExecutable = (Resolve-Path -LiteralPath $ExecutablePath).Path
        $file = Get-Item -LiteralPath $resolvedExecutable
        $executable = [ordered]@{
            Path = $resolvedExecutable
            Length = $file.Length
            FileVersion = $file.VersionInfo.FileVersion
            ProductVersion = $file.VersionInfo.ProductVersion
            SHA256 = (Get-FileHash -LiteralPath $resolvedExecutable -Algorithm SHA256).Hash
        }
    }

    $record = [ordered]@{
        Test = $Test
        StartedLocal = [DateTimeOffset]::Now.ToString('o')
        StartedUtc = [DateTimeOffset]::UtcNow.ToString('o')
        Executable = $executable
        ContextAtStart = Get-ReadOnlyContext
        ContextAtEnd = $null
        EndedLocal = $null
        EndedUtc = $null
        KernelPowerReadError = $null
        KernelPowerEvents = @()
    }

    $record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $recordPath -Encoding UTF8
    Write-Output "Start marker recorded for Test ${Test}: $($record.StartedLocal)"
    Write-Output "Record: $recordPath"
    Write-Output 'No sleep, hibernate, input injection, or power-setting command was issued.'
    return
}

if (-not (Test-Path -LiteralPath $recordPath -PathType Leaf)) {
    throw "No Start marker exists for Test $Test. Start the test before collecting its events."
}

$record = Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json
$started = [DateTimeOffset]::Parse($record.StartedLocal).ToString('o')
$ended = [DateTimeOffset]::Now
$record.EndedLocal = $ended.ToString('o')
$record.EndedUtc = $ended.ToUniversalTime().ToString('o')
$record.ContextAtEnd = Get-ReadOnlyContext

$events = @()
$eventReadError = $null
try {
    $rawEvents = Get-WinEvent -FilterHashtable @{
        LogName = 'System'
        ProviderName = 'Microsoft-Windows-Kernel-Power'
        Id = @(42, 107, 187, 506, 507)
        StartTime = $started
        # ISO 8601 avoids Turkish-localized DateTime strings being rejected by
        # Get-WinEvent's FilterHashtable parser.
        EndTime = $ended.ToString('o')
    }

    foreach ($event in $rawEvents | Sort-Object TimeCreated) {
        $xml = [xml]$event.ToXml()
        $data = [ordered]@{}
        foreach ($node in $xml.Event.EventData.Data) {
            if ($node.Name) {
                $data[[string]$node.Name] = [string]$node.'#text'
            }
        }

        $events += [ordered]@{
            TimeLocal = $event.TimeCreated.ToString('o')
            EventId = $event.Id
            RecordId = $event.RecordId
            EventData = $data
        }
    }
}
catch {
    $eventReadError = $_.Exception.Message
    Write-Warning "Could not read the System Kernel-Power log: $eventReadError"
}

$record.KernelPowerReadError = $eventReadError
$record.KernelPowerEvents = @($events)
$record | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $recordPath -Encoding UTF8

$csvPath = Join-Path $outputDirectory "$testStem-KernelPower.csv"
$events | ForEach-Object {
    [pscustomobject]@{
        TimeLocal = $_.TimeLocal
        EventId = $_.EventId
        ApiCallerName = $_.EventData.ApiCallerName
        SystemAction = $_.EventData.SystemAction
        TargetState = $_.EventData.TargetState
        EffectiveState = $_.EventData.EffectiveState
        WakeFromState = $_.EventData.WakeFromState
        SleepEntered = $_.EventData.SleepEntered
        SleepDurationInUs = $_.EventData.SleepDurationInUs
        Reason = $_.EventData.Reason
    }
} | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8

Write-Output "Events collected for Test $Test."
Write-Output "JSON: $recordPath"
Write-Output "CSV:  $csvPath"
if ($events.Count -eq 0) {
    if ($null -ne $eventReadError) {
        Write-Output 'Event collection failed. See KernelPowerReadError in the JSON; an empty CSV does not mean no power events occurred.'
    }
    else {
        Write-Output 'No matching Kernel-Power events were found in the marked interval.'
    }
}
else {
    $events | ForEach-Object {
        [pscustomobject]@{
            Time = $_.TimeLocal
            Id = $_.EventId
            Caller = $_.EventData.ApiCallerName
            Action = $_.EventData.SystemAction
            Target = $_.EventData.TargetState
            Effective = $_.EventData.EffectiveState
            WakeFrom = $_.EventData.WakeFromState
            SleepEntered = $_.EventData.SleepEntered
            DurationUs = $_.EventData.SleepDurationInUs
        }
    } | Format-Table -AutoSize | Out-String | Write-Output
}
Write-Output 'This script only reads power configuration and event logs; it never requests a power transition.'
