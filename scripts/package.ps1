param(
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')]
    [string]$PackageName = ('bemandi-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$destination = Join-Path $root "dist\$PackageName"
if (Test-Path -LiteralPath $destination) { throw 'Package directory already exists' }
New-Item -ItemType Directory -Path $destination | Out-Null
Copy-Item -LiteralPath "$root\build\bemandi.dll" -Destination $destination
Copy-Item -LiteralPath "$root\config\bemandi.ini" -Destination $destination
Copy-Item -LiteralPath "$root\README.md","$root\README.zh-CN.md","$root\LICENSE","$root\THIRD_PARTY_NOTICES.txt" -Destination $destination
New-Item -ItemType Directory -Path "$destination\docs" | Out-Null
Copy-Item -LiteralPath "$root\docs\game-updates.txt","$root\docs\audio-refactor-validation.txt" -Destination "$destination\docs"
Copy-Item -LiteralPath "$root\docs\images" -Destination "$destination\docs" -Recurse
Copy-Item -LiteralPath "$root\third_party\minhook\LICENSE.txt" -Destination "$destination\MINHOOK-LICENSE.txt"
Copy-Item -LiteralPath "$root\third_party\ndi-headers\NDI SDK License Agreement.pdf" -Destination $destination
Get-Content -LiteralPath "$root\third_party\ndi-headers\Processing.NDI.Lib.h" -TotalCount 28 | Select-Object -Skip 2 | Set-Content -LiteralPath "$destination\NDI-HEADERS-LICENSE.txt" -Encoding UTF8
$hash = Get-FileHash -LiteralPath "$destination\bemandi.dll" -Algorithm SHA256
"$($hash.Hash.ToLower())  bemandi.dll" | Set-Content -LiteralPath "$destination\SHA256SUMS.txt" -Encoding ASCII
Write-Host $destination
