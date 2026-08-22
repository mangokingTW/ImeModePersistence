$ErrorActionPreference = 'Stop'

# The package version drives the download URL, so one script serves every
# release. choco sets this during install; the release workflow bumps the package
# version (choco pack --version) and the $checksum below.
$version  = $env:ChocolateyPackageVersion
# One Inno installer adapts to the host architecture, so the 32- and 64-bit
# slots point at the same asset. Chocolatey runs elevated, so the machine-wide
# (admin) installer is the right variant here.
$url      = "https://github.com/mangokingTW/ImeModePersistence/releases/download/v$version/ImeModePersistence-$version-setup.exe"
$checksum = '7F9C6D417DB6A072AEE0F1B2C5B43AF5E0D5D1003B0C08B6A94EBD7B60C02F1F'

$packageArgs = @{
  packageName    = 'imemodepersistence'
  fileType       = 'exe'
  url            = $url
  url64bit       = $url
  checksum       = $checksum
  checksumType   = 'sha256'
  checksum64     = $checksum
  checksumType64 = 'sha256'
  # Inno Setup silent switches; /ALLUSERS ensures machine-wide install into Program Files; /NORESTART keeps reboot decisions with Chocolatey.
  silentArgs     = '/ALLUSERS /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-'
  validExitCodes = @(0)
  softwareName   = 'ImeModePersistence*'
}

Install-ChocolateyPackage @packageArgs
