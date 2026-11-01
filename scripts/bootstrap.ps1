param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$third = Join-Path $root 'third_party'
New-Item -ItemType Directory -Force -Path $third | Out-Null
$minhook = Join-Path $third 'minhook'
$minhookCommit = 'c3fcafdc10146beb5919319d0683e44e3c30d537'
if (-not (Test-Path "$minhook\.git")) {
    & git clone --depth 1 --branch v1.3.4 https://github.com/TsudaKageyu/minhook.git $minhook
    if ($LASTEXITCODE -ne 0) { throw 'MinHook download failed' }
}
$actual = & git -C $minhook rev-parse HEAD
if ($actual -ne $minhookCommit) { throw 'Unexpected MinHook revision; preserve your checkout and resolve manually' }
$ndi = Join-Path $third 'ndi-headers'
$spice = Join-Path $third 'spice-sdk'
New-Item -ItemType Directory -Force -Path $ndi,$spice | Out-Null
$distroCommit = '68f74d91fd5e782162599ba613ebbe60815df204'
$files = @('Processing.NDI.Lib.h','Processing.NDI.Lib.cplusplus.h','Processing.NDI.DynamicLoad.h',
    'Processing.NDI.structs.h','Processing.NDI.compat.h','Processing.NDI.deprecated.h',
    'Processing.NDI.Find.h','Processing.NDI.Recv.h','Processing.NDI.Recv.ex.h',
    'Processing.NDI.Send.h','Processing.NDI.Routing.h','Processing.NDI.FrameSync.h',
    'Processing.NDI.utilities.h','Processing.NDI.SendListener.h','Processing.NDI.SendAdvertiser.h',
    'Processing.NDI.RecvListener.h','Processing.NDI.RecvAdvertiser.h','Version.txt','NDI SDK License Agreement.pdf')
foreach ($file in $files) {
    $destination = Join-Path $ndi $file
    if (-not (Test-Path -LiteralPath $destination)) {
        $encoded = [uri]::EscapeDataString($file)
        Invoke-WebRequest -UseBasicParsing "https://raw.githubusercontent.com/DistroAV/DistroAV/$distroCommit/lib/ndi/$encoded" -OutFile $destination
    }
}
$spiceCommit = '3e3db7a411c80ff21e8cbee4f7609a8f9b8285b4'
if (-not (Test-Path "$spice\spicesdk.h")) {
    Invoke-WebRequest -UseBasicParsing "https://raw.githubusercontent.com/spice2x/spice2x.github.io/$spiceCommit/src/spice2x/sdk/include/spicesdk.h" -OutFile "$spice\spicesdk.h"
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue) -and -not (Test-Path "$root\.tools\cmake\data\bin\cmake.exe")) {
    & $Python -m pip install --target "$root\.tools" cmake==3.31.6
    if ($LASTEXITCODE -ne 0) { throw 'Local CMake installation failed' }
}
Write-Host 'Build headers and MinHook ready. Install the NDI 6 runtime separately on sender/receiver PCs.'
