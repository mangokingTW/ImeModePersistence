# Combines per-architecture .msix packages into one .msixbundle for the Store.
#
#   powershell -ExecutionPolicy Bypass -File packaging\msix\bundle.ps1 `
#     -Packages out\ImeModePersistence-x64.msix, out\ImeModePersistence-arm64.msix `
#     -OutFile dist\ImeModePersistence-1.5.6.msixbundle
#
# Why a bundle rather than two uploads: Partner Center takes one package set per
# submission and msstore-cli's `publish` accepts a single path, so a bundle is
# how both architectures reach one Store listing. The Store then serves each
# device the package matching its architecture, from the same product page.
#
# The packages must share an Identity (Name, Publisher, Version) and differ only
# in ProcessorArchitecture -- build.ps1 guarantees that, since every package is
# stamped from the same AppxManifest.xml.
param(
  [Parameter(Mandatory = $true)]
  [string[]]$Packages,
  [Parameter(Mandatory = $true)]
  [string]$OutFile,
  # Defaults to the version in AppxManifest.xml, which is the one Partner Center
  # compares against previous submissions. Overriding it risks a bundle whose
  # version disagrees with the packages inside it.
  [string]$BundleVersion
)
$ErrorActionPreference = 'Stop'

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $here 'find-makeappx.ps1')

foreach ($p in $Packages) {
  if (-not (Test-Path $p)) { throw "package not found: $p (run build.ps1 first)" }
}

if (-not $BundleVersion) {
  [xml]$manifest = Get-Content (Join-Path $here 'AppxManifest.xml')
  $BundleVersion = $manifest.Package.Identity.Version
}

# makeappx bundle takes a directory and bundles everything in it, so the inputs
# get their own staging directory -- pointing it at a build output directory
# would sweep in whatever else happens to be there.
$stage = Join-Path ([System.IO.Path]::GetTempPath()) 'imemodepersistence-msixbundle'
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stage | Out-Null
foreach ($p in $Packages) { Copy-Item $p $stage }

$outDir = Split-Path -Parent $OutFile
if ($outDir) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

$makeappx = Find-MakeAppx
Write-Host "Using $makeappx"
Write-Host "Bundling version $BundleVersion from: $($Packages -join ', ')"
& $makeappx bundle /d $stage /p $OutFile /bv $BundleVersion /o
if ($LASTEXITCODE -ne 0) { throw "makeappx bundle failed" }

Write-Host "Bundled: $OutFile"
