param(
    [string]$ExePath = "build-x64/Release/ImeModePersistence.exe"
)

$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")

if (-not (Test-Path (Join-Path $root $ExePath))) {
    Write-Error "Executable not found at '$ExePath'. Please build Release first:`n  cmake --build build-x64 --config Release"
    exit 1
}

Write-Host "=== 1. Stopping existing instances if running ===" -ForegroundColor Cyan
if (Test-Path "build-x64/Release/ImeModePersistence.exe") {
    & "build-x64/Release/ImeModePersistence.exe" --stop-helper | Out-Null
}
Get-Process ImeModePersistence -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Get-Process ImeModePersistenceHelper -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 400

Write-Host "=== 2. Preparing local MSIX staging folder ===" -ForegroundColor Cyan
$stage = Join-Path $root "build-msix"
if (Test-Path $stage) {
    Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Path $stage -Force | Out-Null

Copy-Item (Join-Path $root $ExePath) (Join-Path $stage "ImeModePersistence.exe")
Copy-Item (Join-Path $PSScriptRoot "AppxManifest.xml") (Join-Path $stage "AppxManifest.xml")
Copy-Item (Join-Path $PSScriptRoot "assets") (Join-Path $stage "assets") -Recurse

Write-Host "=== 3. Registering package with Windows AppX ===" -ForegroundColor Cyan
$manifestPath = Join-Path $stage "AppxManifest.xml"

try {
    Add-AppxPackage -Register $manifestPath
    Write-Host "`n[SUCCESS] Installed local MSIX package successfully!" -ForegroundColor Green
    Write-Host "Package Name: MangoYen.IMEModePersistence"
    Write-Host "`nYou can now find 'IME Mode Persistence' in your Windows Start Menu, or launch it with:"
    Write-Host "  Start-Process shell:AppsFolder\MangoYen.IMEModePersistence_2zs50d2afav02!ImeModePersistence" -ForegroundColor Yellow
} catch {
    Write-Warning "Registration failed. If Developer Mode is required, enable it in Windows Settings > System > For developers."
    Write-Error $_
}
