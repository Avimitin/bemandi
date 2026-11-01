param(
    [ValidateRange(5,600)][int]$Seconds = 15,
    [ValidateRange(0,120)][int]$StartDelaySeconds = 0,
    [ValidateSet('streaming-on','streaming-off','unspecified')][string]$Label = 'unspecified',
    [string]$OutputPath = ''
)
# Read-only process/system measurements. Does not change game/NDI settings.
$ErrorActionPreference = 'Stop'
if (-not $OutputPath) {
    $OutputPath = Join-Path $PSScriptRoot "bemandi-metrics-$Label-$(Get-Date -Format 'yyyyMMdd-HHmmss-fff').json"
}
if (Test-Path -LiteralPath $OutputPath) { throw 'Output already exists; preserving previous measurements.' }
$games = @(Get-Process -Name spice64 -ErrorAction SilentlyContinue)
if ($games.Count -ne 1) { throw 'Expected one running spice64 process.' }
$gameProcess = $games[0]
$gamePid = $gameProcess.Id
$logicalCpus = [int](Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
$cpuName = @(Get-CimInstance Win32_Processor | Select-Object -ExpandProperty Name)
$nvCommand = Get-Command nvidia-smi.exe -ErrorAction SilentlyContinue
if ($StartDelaySeconds -gt 0) {
    Write-Host "Return to the game. Measurement starts in $StartDelaySeconds seconds and lasts about $Seconds seconds."
    Start-Sleep -Seconds $StartDelaySeconds
}
Write-Host "Measuring: $Label. Keep playing normally."
$samples = @()
$warnings = New-Object 'System.Collections.Generic.HashSet[string]'
$clock = [Diagnostics.Stopwatch]::StartNew()
$gameProcess.Refresh()
$previousCpu = $gameProcess.TotalProcessorTime.TotalSeconds
$previousTime = $clock.Elapsed.TotalSeconds
while ($clock.Elapsed.TotalSeconds -lt $Seconds) {
    Start-Sleep -Seconds 1
    $gameProcess.Refresh()
    if ($gameProcess.HasExited) { break }
    $now = $clock.Elapsed.TotalSeconds
    $cpu = $gameProcess.TotalProcessorTime.TotalSeconds
    $corePercent = 100.0 * ($cpu-$previousCpu) / ($now-$previousTime)
    $previousCpu=$cpu
    $previousTime=$now
    $gpu = @()
    if ($nvCommand) {
        try {
            $lines = & $nvCommand.Source --query-gpu=index,name,utilization.gpu,memory.used,memory.total --format=csv,noheader,nounits 2>$null
            if ($LASTEXITCODE -eq 0) {
                $gpu = @($lines | ConvertFrom-Csv -Header index,name,utilization_percent,memory_used_mib,memory_total_mib)
            } else { $null=$warnings.Add('nvidia-smi failed; GPU totals unavailable') }
        } catch { $null=$warnings.Add('nvidia-smi failed; GPU totals unavailable') }
    } else { $null=$warnings.Add('nvidia-smi not found; GPU totals unavailable') }
    $processGpuMemory = $null
    try {
        $memory = @(Get-CimInstance -ClassName Win32_PerfFormattedData_GPUPerformanceCounters_GPUProcessMemory -Filter "Name LIKE 'pid_${gamePid}_%'" -OperationTimeoutSec 2)
        if ($memory.Count) {
            $processGpuMemory = [math]::Round((($memory | Measure-Object DedicatedUsage -Sum).Sum / 1MB),2)
        }
    } catch { $null=$warnings.Add('GPU process-memory counters unavailable') }
    $samples += [pscustomobject]@{
        timestamp=(Get-Date -Format o)
        spice_cpu_one_core_percent=[math]::Round($corePercent,2)
        spice_cpu_machine_percent=[math]::Round($corePercent/$logicalCpus,2)
        spice_working_set_mib=[math]::Round($gameProcess.WorkingSet64/1MB,2)
        spice_private_commit_mib=[math]::Round($gameProcess.PrivateMemorySize64/1MB,2)
        spice_dedicated_gpu_memory_mib=$processGpuMemory
        gpu_total=$gpu
    }
}
if (-not $samples.Count) { throw 'No measurements collected' }
$record = [pscustomobject]@{
    label=$Label
    requested_duration_seconds=$Seconds
    actual_duration_seconds=[math]::Round($clock.Elapsed.TotalSeconds,3)
    process_id=$gamePid
    cpu_name=$cpuName
    logical_cpus=$logicalCpus
    note='spice64 values include the game and every loaded plugin; GPU totals include all applications. These are not bemandi-only costs. Read alongside capture counters and compare matched scenes with/without an NDI receiver.'
    average_spice_cpu_machine_percent=[math]::Round(($samples | Measure-Object spice_cpu_machine_percent -Average).Average,2)
    average_spice_cpu_one_core_percent=[math]::Round(($samples | Measure-Object spice_cpu_one_core_percent -Average).Average,2)
    warnings=@($warnings | ForEach-Object { $_ })
    samples=$samples
}
$record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
Write-Host "Saved $OutputPath"
Write-Host "Spice average CPU: $($record.average_spice_cpu_machine_percent)% of machine ($($record.average_spice_cpu_one_core_percent)% of one logical core)."
