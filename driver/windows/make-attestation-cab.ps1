# Packs the unsigned driver (the CI artifact VocalInkAudio-x64-unsigned) into the CAB
# that Microsoft's attestation signing expects, and signs the CAB with the EV
# code-signing certificate of the Partner Center account. Run in a Developer
# PowerShell (makecab and signtool on PATH) on the machine that has the EV token.
#
#   pwsh driver/windows/make-attestation-cab.ps1 -PackageDir .\VocalInkAudio-x64-unsigned -CertThumbprint <EV cert SHA-1>
#
# Then submit dist\VocalInkAudio.cab in Partner Center (Hardware > Submit new hardware),
# download the signed result and put VocalInkAudio.inf/.sys/.cat, unmodified, in
# packaging/windows/driver/. See docs/VIRTUAL_AUDIO.md.
param(
    [Parameter(Mandatory = $true)][string]$PackageDir,
    [string]$CertThumbprint,
    [string]$OutDir = "dist",
    [string]$TimestampUrl = "http://timestamp.digicert.com"
)
$ErrorActionPreference = "Stop"

$pkg = (Resolve-Path $PackageDir).Path
$files = @("VocalInkAudio.inf", "VocalInkAudio.sys")
foreach ($f in $files) {
    if (-not (Test-Path (Join-Path $pkg $f))) { throw "$f is missing in $pkg" }
}
if (Test-Path (Join-Path $pkg "VocalInkAudio.pdb")) { $files += "VocalInkAudio.pdb" }

New-Item -ItemType Directory -Force $OutDir | Out-Null
$out = (Resolve-Path $OutDir).Path
$ddf = Join-Path ([IO.Path]::GetTempPath()) "VocalInkAudio.ddf"
$lines = @(
    ".OPTION EXPLICIT",
    ".Set CabinetFileCountThreshold=0",
    ".Set FolderFileCountThreshold=0",
    ".Set FolderSizeThreshold=0",
    ".Set MaxCabinetSize=0",
    ".Set MaxDiskFileCount=0",
    ".Set MaxDiskSize=0",
    ".Set CompressionType=MSZIP",
    ".Set Cabinet=on",
    ".Set Compress=on",
    ".Set InfFileName=nul",
    ".Set RptFileName=nul",
    ".Set CabinetNameTemplate=VocalInkAudio.cab",
    ".Set DiskDirectoryTemplate=`"$out`"",
    # One folder per driver inside the CAB.
    ".Set DestinationDir=VocalInkAudio"
) + ($files | ForEach-Object { "`"$(Join-Path $pkg $_)`"" })
Set-Content -Path $ddf -Value $lines -Encoding ascii

makecab /f $ddf
if ($LASTEXITCODE -ne 0) { throw "makecab failed" }
$cab = Join-Path $out "VocalInkAudio.cab"

if ($CertThumbprint) {
    signtool sign /v /fd sha256 /tr $TimestampUrl /td sha256 /sha1 $CertThumbprint $cab
    if ($LASTEXITCODE -ne 0) { throw "signing the CAB failed" }
} else {
    Write-Warning "The CAB is not signed yet: Partner Center only accepts it signed with your EV certificate (-CertThumbprint)."
}
Write-Host "Created $cab"
