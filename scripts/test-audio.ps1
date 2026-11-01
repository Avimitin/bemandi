$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$output = Join-Path $root 'test-output'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$test = Start-Process -FilePath "$root\build\audio_integration_test.exe" -PassThru -WindowStyle Hidden -RedirectStandardError "$output\audio-test.log" -RedirectStandardOutput "$output\audio-result.json"
$null = $test.Handle
if (-not $test.WaitForExit(30000)) { $test.Kill(); throw 'Audio integration test timed out' }
$test.WaitForExit()
if ($test.ExitCode -ne 0) { throw "Audio integration test failed; see $output" }
Get-Content "$output\audio-result.json"
