# Microsoft Store (MSIX) build

A packaged, full-trust desktop build for the Microsoft Store. The Store signs it
on upload, so there is no code-signing cost and it does not trip SmartScreen or
antivirus the way the unsigned installer can.

## Important limitation

**A Store MSIX cannot elevate.** It therefore **cannot control administrator or
anti-cheat-protected targets** (e.g. Helldivers 2). The Store build is for the
ordinary use cases — mode persistence across windows, binding non-elevated
applications, and the caret indicator. Users who need to bind protected games
should install the direct-download / Scoop / winget / Chocolatey build instead.
The listing and description must say this clearly.

## Prerequisites (must be done by the account owner)

1. **Partner Center developer account** — one-time US$19 (individual):
   https://partner.microsoft.com/dashboard/registration
2. **Reserve the app name** (Apps and games > New product). This yields the
   package identity. From *Product > Product identity*, copy into
   `AppxManifest.xml`:
   - `Name`                 → `Package/Identity/@Name`
   - `Publisher` (`CN=...`) → `Package/Identity/@Publisher`
   - `PublisherDisplayName` → `Package/Properties/PublisherDisplayName`

Send me those three values and I will fill them in.

## Build

```powershell
cmake -S . -B build-x64 -A x64 && cmake --build build-x64 --config Release
pwsh packaging/msix/build.ps1        # needs makeappx.exe (Windows SDK) on PATH
```

Produces `dist/ImeModePersistence.msix`.

## Test locally (sideload)

The Store signs on upload, but to run the package on your own machine first,
sign it with a self-signed cert whose subject exactly equals the manifest's
`Identity/@Publisher`, trust that cert, then `Add-AppxPackage`.

## Submit

Upload the unsigned `.msix` in Partner Center (it re-signs with the Store
certificate), fill in the listing (description, screenshots, the elevation
limitation note), and submit for certification.

## Remaining code work (Phase 2, before a real submission)

The current app assumes an unpackaged install. For a correct MSIX build:

- **Detect packaged context** (`GetCurrentPackageFullName` succeeds when packaged).
- **Autostart** — when packaged, drive the tray "start at logon" toggle through
  the `Windows.ApplicationModel.StartupTask` API (this manifest declares the
  `windows.startupTask` extension) instead of the HKCU Run key, which MSIX
  virtualizes to no effect.
- **Elevation UI** — hide **Restart as administrator** and the "needs
  administrator" notification when packaged, since MSIX cannot elevate.
- The Inno installer is not used for this channel.

These need a Windows machine to test; they are tracked for when the account and
package identity are in place.
