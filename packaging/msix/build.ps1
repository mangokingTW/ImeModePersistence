# Packs a Store MSIX for one architecture from a built executable, the manifest
# and the assets. Run on Windows with the Windows SDK installed (this script
# locates makeappx.exe itself). In Windows PowerShell, run each line separately
# -- 5.1 has no '&&':
#
#   cmake -S . -B build-x64 -A x64
#   cmake --build build-x64 --config Release
#   powershell -ExecutionPolicy Bypass -File packaging\msix\build.ps1
#
# For ARM64, point it at an ARM64 build and say so -- the manifest's
# ProcessorArchitecture is rewritten to match, everything else is identical
# (same Identity, so both packages belong to the same Store listing):
#
#   cmake -S . -B build-arm64 -A ARM64
#   cmake --build build-arm64 --config Release
#   powershell -ExecutionPolicy Bypass -File packaging\msix\build.ps1 -Architecture arm64 -ExePath build-arm64/Release/ImeModePersistence.exe -OutName ImeModePersistence-arm64.msix
#
# To ship both architectures under one Store submission, combine the two
# packages with bundle.ps1 -- Partner Center takes a single upload, and the
# Store hands each device the package matching its architecture.
#
# The Store
# signs the package on upload, so the .msix produced here does not need signing
# for submission; sign it with a self-signed cert only to sideload-test locally.
param(
  [string]$ExePath = "build-x64/Release/ImeModePersistence.exe",
  [string]$OutDir  = "dist",
  [ValidateSet('x64', 'arm64')]
  [string]$Architecture = 'x64',
  [string]$OutName = 'ImeModePersistence.msix'
)
$ErrorActionPreference = 'Stop'

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $here 'find-makeappx.ps1')
if (-not (Test-Path $ExePath)) { throw "executable not found: $ExePath (build Release first)" }

$stage = Join-Path ([System.IO.Path]::GetTempPath()) "imemodepersistence-msix-$Architecture"
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stage | Out-Null

Copy-Item $ExePath                                (Join-Path $stage 'ImeModePersistence.exe')
Copy-Item (Join-Path $here 'AppxManifest.xml')    (Join-Path $stage 'AppxManifest.xml')
Copy-Item (Join-Path $here 'assets')              (Join-Path $stage 'assets') -Recurse

# The checked-in manifest declares x64; rewrite the staged copy for other
# targets. Edited as XML rather than by string replacement so a stray "x64"
# anywhere else in the file can never be clobbered.
$manifestPath = Join-Path $stage 'AppxManifest.xml'
[xml]$manifest = Get-Content $manifestPath
$identity = $manifest.Package.Identity
if ($identity.ProcessorArchitecture -ne $Architecture) {
  $identity.ProcessorArchitecture = $Architecture
  $manifest.Save($manifestPath)
}
Write-Host "Packaging $Architecture (manifest version $($identity.Version))"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$msix = Join-Path $OutDir $OutName

$makeappx = Find-MakeAppx
Write-Host "Using $makeappx"
& $makeappx pack /d $stage /p $msix /o
if ($LASTEXITCODE -ne 0) { throw "makeappx failed" }

Write-Host "Packed: $msix"
Write-Host "Sideload test (needs a self-signed cert whose subject = Identity/Publisher):"
Write-Host "  signtool sign /fd SHA256 /f test.pfx /p <pwd> `"$msix`""
Write-Host "Store: upload this .msix in Partner Center - Microsoft signs it."
