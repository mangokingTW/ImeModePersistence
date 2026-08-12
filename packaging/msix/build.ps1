# Packs the Store MSIX from a built executable, the manifest and the assets.
# Run on Windows with the Windows SDK installed (makeappx.exe on PATH, e.g.
# "C:\Program Files (x86)\Windows Kits\10\bin\<ver>\x64").
#
#   cmake -S . -B build-x64 -A x64 && cmake --build build-x64 --config Release
#   pwsh packaging/msix/build.ps1
#
# Fill in the real Identity in AppxManifest.xml first (see README.md). The Store
# signs the package on upload, so the .msix produced here does not need signing
# for submission; sign it with a self-signed cert only to sideload-test locally.
param(
  [string]$ExePath = "build-x64/Release/ImeModePersistence.exe",
  [string]$OutDir  = "dist"
)
$ErrorActionPreference = 'Stop'

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not (Test-Path $ExePath)) { throw "executable not found: $ExePath (build Release first)" }

$stage = Join-Path ([System.IO.Path]::GetTempPath()) 'imemodepersistence-msix'
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stage | Out-Null

Copy-Item $ExePath                                (Join-Path $stage 'ImeModePersistence.exe')
Copy-Item (Join-Path $here 'AppxManifest.xml')    (Join-Path $stage 'AppxManifest.xml')
Copy-Item (Join-Path $here 'assets')              (Join-Path $stage 'assets') -Recurse

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$msix = Join-Path $OutDir 'ImeModePersistence.msix'

makeappx pack /d $stage /p $msix /o
if ($LASTEXITCODE -ne 0) { throw "makeappx failed" }

Write-Host "Packed: $msix"
Write-Host "Sideload test (needs a self-signed cert whose subject = Identity/Publisher):"
Write-Host "  signtool sign /fd SHA256 /f test.pfx /p <pwd> `"$msix`""
Write-Host "Store: upload this .msix in Partner Center - Microsoft signs it."
