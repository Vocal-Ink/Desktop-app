# Collects VocalInk.exe with Qt and the MSVC runtime into a folder, then builds
# a portable zip and (if Inno Setup is installed) an installer.
#
#   pwsh packaging/windows/deploy.ps1 -BuildDir build -Version 0.1.0
#
# Vocal Ink's virtual mic driver is included only when the Microsoft-signed
# package (VocalInkAudio.inf/.sys/.cat, see docs/VIRTUAL_AUDIO.md) is in
# packaging/windows/driver, or when VOCALINK_DRIVER_PACKAGE_URL (+ _SHA256)
# points to a zip of it. It goes to <app>\driver together with nefconw.exe.
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

# Qt libraries and plugins (multimedia + FFmpeg, texttospeech, tls, platforms...)
# and the Qt Quick modules the interface imports (scanned from src/qml).
windeployqt --release --no-translations --no-system-d3d-compiler --no-opengl-sw `
    --qmldir (Join-Path $Root "src\qml") (Join-Path $Stage "VocalInk.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

# App-local MSVC runtime so users don't need to install the redistributable.
if ($env:VCToolsRedistDir) {
    $crt = Get-ChildItem -Path (Join-Path $env:VCToolsRedistDir "x64") -Directory -Filter "Microsoft.VC*.CRT" | Select-Object -First 1
    if ($crt) { Copy-Item (Join-Path $crt.FullName "*.dll") $Stage }
}

Copy-Item (Join-Path $Root "LICENSE") $Stage
Copy-Item (Join-Path $Root "THIRD_PARTY_NOTICES.md") $Stage

# --- Virtual mic driver (optional) ----------------------------------------------------
# nefcon (MIT) installs it: https://github.com/nefarius/nefcon, pinned and hash-checked.
$NefconVersion = "v1.21.0"
$NefconSha256 = "4c8e68f6b663598d77b7c3f76459b16fad00994e53ea8af0bd106a805dd5c18b"

function Get-Nefconw {
    $zip = Join-Path ([IO.Path]::GetTempPath()) "nefcon_$NefconVersion.zip"
    $url = "https://github.com/nefarius/nefcon/releases/download/$NefconVersion/nefcon_$NefconVersion.zip"
    Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
    $hash = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLowerInvariant()
    if ($hash -ne $NefconSha256) { throw "nefcon $NefconVersion has SHA-256 $hash, expected $NefconSha256" }
    $dir = Join-Path ([IO.Path]::GetTempPath()) "nefcon_$NefconVersion"
    Expand-Archive -Path $zip -DestinationPath $dir -Force
    return (Join-Path $dir "x64\nefconw.exe")
}

$DriverSrc = Join-Path $PSScriptRoot "driver"
$DriverFiles = "VocalInkAudio.inf", "VocalInkAudio.sys", "VocalInkAudio.cat"
$HaveDriver = { ($DriverFiles | Where-Object { -not (Test-Path (Join-Path $DriverSrc $_)) }).Count -eq 0 }
if (-not (& $HaveDriver) -and $env:VOCALINK_DRIVER_PACKAGE_URL) {
    if (-not $env:VOCALINK_DRIVER_PACKAGE_SHA256) { throw "VOCALINK_DRIVER_PACKAGE_SHA256 must be set with VOCALINK_DRIVER_PACKAGE_URL" }
    $zip = Join-Path ([IO.Path]::GetTempPath()) "VocalInkAudio-signed.zip"
    Invoke-WebRequest -Uri $env:VOCALINK_DRIVER_PACKAGE_URL -OutFile $zip -UseBasicParsing
    $hash = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLowerInvariant()
    if ($hash -ne $env:VOCALINK_DRIVER_PACKAGE_SHA256.ToLowerInvariant()) { throw "The signed driver package has SHA-256 $hash" }
    $unzipped = Join-Path ([IO.Path]::GetTempPath()) "VocalInkAudio-signed"
    Expand-Archive -Path $zip -DestinationPath $unzipped -Force
    New-Item -ItemType Directory -Force $DriverSrc | Out-Null
    foreach ($f in $DriverFiles) {
        $found = Get-ChildItem $unzipped -Recurse -Filter $f | Select-Object -First 1
        if ($found) { Copy-Item $found.FullName $DriverSrc }
    }
}
if (& $HaveDriver) {
    # The Microsoft-signed files are copied as they are: changing them breaks the signature.
    $DriverStage = Join-Path $Stage "driver"
    New-Item -ItemType Directory -Force $DriverStage | Out-Null
    foreach ($f in $DriverFiles) { Copy-Item (Join-Path $DriverSrc $f) $DriverStage }
    $nefconw = Join-Path $DriverSrc "nefconw.exe"
    if (-not (Test-Path $nefconw)) { $nefconw = Get-Nefconw }
    Copy-Item $nefconw (Join-Path $DriverStage "nefconw.exe")
    Write-Host "Including the Vocal Ink virtual mic driver"
} else {
    Write-Host "No signed virtual mic driver in packaging/windows/driver; the app will suggest VB-CABLE."
}

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
