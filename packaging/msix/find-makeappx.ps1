# Shared makeappx.exe lookup for build.ps1 and bundle.ps1. Dot-source it:
#
#   . (Join-Path $PSScriptRoot 'find-makeappx.ps1')
#   $makeappx = Find-MakeAppx
#
# makeappx.exe is rarely on PATH; find the newest x64 one in the Windows SDK.
# The x64 tool packs ARM64 payloads fine -- makeappx only copies files and
# writes the manifest, so the host architecture is irrelevant.
function Find-MakeAppx {
  $cmd = Get-Command makeappx.exe -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }
  foreach ($root in @("${env:ProgramFiles(x86)}\Windows Kits\10\bin", "${env:ProgramFiles}\Windows Kits\10\bin")) {
    if (Test-Path $root) {
      $exe = Get-ChildItem $root -Recurse -Filter makeappx.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\x64\\' } |
        Sort-Object FullName -Descending | Select-Object -First 1
      if ($exe) { return $exe.FullName }
    }
  }
  throw "makeappx.exe not found. Install the Windows SDK, or run from the 'Developer PowerShell for VS'."
}
