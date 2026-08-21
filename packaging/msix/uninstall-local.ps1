$ErrorActionPreference = 'SilentlyContinue'

Write-Host "=== Stopping running instances ===" -ForegroundColor Cyan
Get-Process ImeModePersistence -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Get-Process ImeModePersistenceHelper -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

Write-Host "=== Removing MSIX Package ===" -ForegroundColor Cyan
$packages = Get-AppxPackage *IMEModePersistence*
if ($packages) {
    foreach ($pkg in $packages) {
        Write-Host "Removing: $($pkg.PackageFullName)"
        Remove-AppxPackage -Package $pkg.PackageFullName
    }
    Write-Host "[SUCCESS] Uninstalled local MSIX package." -ForegroundColor Green
} else {
    Write-Host "No installed MSIX package found." -ForegroundColor Yellow
}
