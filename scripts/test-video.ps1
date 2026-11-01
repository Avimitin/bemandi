param([int]$Seconds = 8)
$ErrorActionPreference = 'Stop'
if ($Seconds -lt 4 -or $Seconds -gt 120) { throw 'Seconds must be 4..120' }
$root = Split-Path $PSScriptRoot
$output = Join-Path $root 'test-output'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$sender = Join-Path $root 'build\capture_test.exe'
$receiver = Join-Path $root 'build\ndi_receive_test.exe'
$dll = Join-Path $root 'build\bemandi.dll'
$rows = @()
$cases = @(
    @{ Name='baseline'; Format='uyvy'; Extra='--baseline'; Receive=$false },
    @{ Name='idle'; Format='uyvy'; Extra=''; Receive=$false },
    @{ Name='uyvy'; Format='uyvy'; Extra=''; Receive=$true },
    @{ Name='bgra'; Format='bgra'; Extra=''; Receive=$true },
    @{ Name='reset'; Format='uyvy'; Extra='--reset'; Receive=$true },
    @{ Name='legacy'; Format='uyvy'; Extra='--legacy --reset'; Receive=$true },
    @{ Name='swap'; Format='uyvy'; Extra='--swap'; Receive=$true },
    @{ Name='slow-receiver'; Format='uyvy'; Extra=''; Receive=$true; Slow=100 }
)
foreach ($case in $cases) {
    $name = $case.Name
    $source = "Bemandi-Test-$name"
    $ini = Join-Path $output "$name.ini"
    @"
[video]
format=$($case.Format)
fps=120
width=1920
height=1080
swapchain=0
source_name=$source
"@ | Set-Content -LiteralPath $ini -Encoding Unicode
    $rx = $null
    try {
        if ($case.Receive) {
            $slow = if ($case.Slow) { $case.Slow } else { 0 }
            $rx = Start-Process -FilePath $receiver -ArgumentList "`"$source`" $($Seconds+3) $slow" -PassThru -WindowStyle Hidden -RedirectStandardOutput "$output\$name-rx.json" -RedirectStandardError "$output\$name-rx.err"
            $null = $rx.Handle
        }
        $tx = Start-Process -FilePath $sender -ArgumentList "--dll `"$dll`" --ini `"$ini`" --seconds $Seconds $($case.Extra)" -PassThru -WindowStyle Hidden -RedirectStandardOutput "$output\$name-tx.json" -RedirectStandardError "$output\$name-tx.err"
        $null = $tx.Handle
        if (-not $tx.WaitForExit(($Seconds+20)*1000)) { $tx.Kill(); throw "$name sender timed out" }
        $tx.WaitForExit()
        if ($tx.ExitCode -ne 0) { throw "$name sender failed: $(Get-Content "$output\$name-tx.err" -Raw)" }
        $txResult = Get-Content "$output\$name-tx.json" -Raw | ConvertFrom-Json
        $rxResult = $null
        if ($rx) {
            if (-not $rx.WaitForExit(10000)) { $rx.Kill(); throw "$name receiver timed out" }
            $rx.WaitForExit()
            if ($rx.ExitCode -ne 0) { throw "$name receiver failed: $(Get-Content "$output\$name-rx.json" -Raw) $(Get-Content "$output\$name-rx.err" -Raw)" }
            $rxResult = Get-Content "$output\$name-rx.json" -Raw | ConvertFrom-Json
            if ($txResult.sent -lt 20) { throw "$name produced too few frames" }
        } elseif ($txResult.captured -ne 0) { throw "$name unexpectedly captured without a receiver" }
        $rows += [pscustomobject]@{ case=$name; sender=$txResult; receiver=$rxResult }
        Write-Host "$name passed: sent=$($txResult.sent), busy=$($txResult.busy), CPU=$($txResult.cpu_core_percent)% of one core"
    } finally {
        if ($rx -and -not $rx.HasExited) { $rx.Kill() }
    }
}
$rows | ConvertTo-Json -Depth 6 | Set-Content "$output\results.json" -Encoding UTF8
Write-Host "Results: $output\results.json"
