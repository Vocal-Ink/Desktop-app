# Collects VocalInk.exe with Qt and the MSVC runtime into a folder, then builds
# a portable zip and (if Inno Setup is installed) an installer.
#
#   pwsh packaging/windows/deploy.ps1 -BuildDir build -Version 0.1.0
param(
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [string]$Version = "dev"
)
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
$Dist = Join-Path $Root "dist"
$Stage = Join-Path $Dist "windows\VocalInk"

Remove-Item -Recurse -Force $Stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $Stage | Out-Null

$Exe = Get-ChildItem -Path $BuildDir -Recurse -Filter VocalInk.exe | Select-Object -First 1
if (-not $Exe) { throw "VocalInk.exe not found under $BuildDir" }
Copy-Item $Exe.FullName $Stage

# Qt libraries and plugins (multimedia + FFmpeg, texttospeech, tls, platforms...).
windeployqt --release --no-translations --no-system-d3d-compiler --no-opengl-sw (Join-Path $Stage "VocalInk.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

# App-local MSVC runtime so users don't need to install the redistributable.
if ($env:VCToolsRedistDir) {
    $crt = Get-ChildItem -Path (Join-Path $env:VCToolsRedistDir "x64") -Directory -Filter "Microsoft.VC*.CRT" | Select-Object -First 1
    if ($crt) { Copy-Item (Join-Path $crt.FullName "*.dll") $Stage }
}

Copy-Item (Join-Path $Root "LICENSE") $Stage
Copy-Item (Join-Path $Root "THIRD_PARTY_NOTICES.md") $Stage

# Portable zip: a portable.txt next to the exe keeps settings and models in .\data.
$PortableStage = Join-Path $Dist "windows\VocalInk-portable"
Remove-Item -Recurse -Force $PortableStage -ErrorAction SilentlyContinue
Copy-Item -Recurse $Stage $PortableStage
Set-Content -Path (Join-Path $PortableStage "portable.txt") -Value "Settings, keys and downloaded voices are stored in the data folder next to VocalInk.exe."
$Zip = Join-Path $Dist "VocalInk-$Version-windows-x64-portable.zip"
Remove-Item -Force $Zip -ErrorAction SilentlyContinue
Compress-Archive -Path "$PortableStage\*" -DestinationPath $Zip
Write-Host "Created $Zip"

$Iscc = Get-Command iscc -ErrorAction SilentlyContinue
if (-not $Iscc) {
    $candidate = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
    if (Test-Path $candidate) { $Iscc = Get-Item $candidate }
}
if ($Iscc) {
    & $Iscc.Source "/DAppVersion=$Version" "/DSourceDir=$Stage" "/DOutputDir=$Dist" (Join-Path $PSScriptRoot "VocalInk.iss")
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed" }
} else {
    Write-Warning "Inno Setup (iscc) not found; skipping the installer."
}
