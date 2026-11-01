param(
    [string]$SdkRoot = '',
    [string]$SpiceSdkInclude = '',
    [string]$NdiInclude = '',
    [string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ tools are required.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$lines = & $env:ComSpec /d /c "`"$vcvars`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'vcvars64 failed' }
foreach ($line in $lines) {
    $pair = $line -split '=', 2
    if ($pair.Length -eq 2 -and $pair[0] -notmatch '^=') {
        [Environment]::SetEnvironmentVariable($pair[0], $pair[1], 'Process')
    }
}
if ($SdkRoot) {
    $inc = Get-ChildItem "$SdkRoot\microsoft.windows.sdk.cpp\c\Include" -Directory | Sort-Object Name -Descending | Select-Object -First 1
    $kernel = Get-ChildItem "$SdkRoot\microsoft.windows.sdk.cpp.x64" -Filter kernel32.lib -Recurse | Select-Object -First 1
    $ucrt = Join-Path $SdkRoot 'microsoft.windows.sdk.cpp.x64\c\ucrt\x64'
    if (-not $inc -or -not $kernel -or -not (Test-Path "$ucrt\ucrt.lib")) { throw 'Incomplete Windows SDK' }
    $env:INCLUDE = "$($inc.FullName)\ucrt;$($inc.FullName)\shared;$($inc.FullName)\um;$($inc.FullName)\winrt;$env:INCLUDE"
    $env:LIB = "$($kernel.DirectoryName);$ucrt;$env:LIB"
    $env:PATH = "$SdkRoot\microsoft.windows.sdk.cpp\c\bin\$($inc.Name)\x64;$env:PATH"
}
$cmake = Join-Path $root '.tools\cmake\data\bin\cmake.exe'
if (-not (Test-Path $cmake)) { $cmake = (Get-Command cmake -ErrorAction Stop).Source }
if (-not $SpiceSdkInclude) { $SpiceSdkInclude = Join-Path $root 'third_party\spice-sdk' }
if (-not $NdiInclude) { $NdiInclude = Join-Path $root 'third_party\ndi-headers' }
$argsList = @('-S', $root, '-B', "$root\build", '-G', 'NMake Makefiles', "-DCMAKE_BUILD_TYPE=$Configuration", "-DSPICE_SDK_INCLUDE_DIR=$SpiceSdkInclude", "-DNDI_INCLUDE_DIR=$NdiInclude")
& $cmake @argsList
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& $cmake --build "$root\build"
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
& (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir "$root\build" --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
