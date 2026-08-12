$ErrorActionPreference = 'Stop'

$version  = '1.0.0'
# One Inno installer adapts to the host architecture, so the 32- and 64-bit
# slots point at the same asset. Chocolatey runs elevated, so the machine-wide
# (admin) installer is the right variant here.
$url      = "https://github.com/mangokingTW/ImeModePersistence/releases/download/v$version/ImeModePersistence-$version-setup-admin.exe"
$checksum = '939C1F6237C514FB24D50B9F1720C461B46BF031E2131421871E2DF923511DAD'

$packageArgs = @{
  packageName    = 'imemodepersistence'
  fileType       = 'exe'
  url            = $url
  url64bit       = $url
  checksum       = $checksum
  checksumType   = 'sha256'
  checksum64     = $checksum
  checksumType64 = 'sha256'
  # Inno Setup silent switches; /NORESTART keeps reboot decisions with Chocolatey.
  silentArgs     = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-'
  validExitCodes = @(0)
  softwareName   = 'ImeModePersistence*'
}

Install-ChocolateyPackage @packageArgs
