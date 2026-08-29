# Microsoft Store (MSIX) build

A packaged, full-trust desktop build for the Microsoft Store. The Store signs it
on upload, so it needs no certificate of its own and does not trip SmartScreen or
antivirus the way the unsigned installer can.

## Important limitation

**A Store MSIX cannot elevate.** It therefore **cannot control administrator or
anti-cheat-protected targets** (e.g. Helldivers 2). The Store build is for the
ordinary use cases — mode persistence across windows, binding non-elevated
applications, and the caret indicator. Users who need to bind protected games
should install the direct-download / Scoop / winget / Chocolatey build instead.
The listing and description must say this clearly.

## Product identity (reserved)

The app name is reserved in Partner Center and its identity is set in
`AppxManifest.xml`:

| Field | Value |
|---|---|
| Identity Name | `MangoYen.IMEModePersistence` |
| Publisher | `CN=B64E145E-DB3F-473D-9BA6-BDF6CF2E8081` |
| PublisherDisplayName | `Mango Yen` |
| Package Family Name | `MangoYen.IMEModePersistence_2zs50d2afav02` |
| Store ID | `9P05QQZ2P5XC` |

## Build

In **Windows PowerShell** (run each line separately — 5.1 has no `&&`):

```powershell
cmake -S . -B build-x64 -A x64
cmake --build build-x64 --config Release
powershell -ExecutionPolicy Bypass -File packaging\msix\build.ps1
```

`build.ps1` locates `makeappx.exe` in the Windows SDK itself. If it reports it
cannot find it, install the Windows SDK or run the commands from **Developer
PowerShell for VS**. Produces `dist/ImeModePersistence.msix`.

### Both architectures (what CI submits)

`build.ps1` packs one architecture per run, rewriting the staged manifest's
`ProcessorArchitecture`; `bundle.ps1` combines the results into the single
`.msixbundle` a Store submission takes. Both packages keep the same Identity, so
they are one listing and the Store serves each device its own architecture.

```powershell
cmake -S . -B build-arm64 -A ARM64
cmake --build build-arm64 --config Release
powershell -ExecutionPolicy Bypass -File packaging\msix\build.ps1 -Architecture x64 -ExePath build-x64/Release/ImeModePersistence.exe -OutDir msix-packages -OutName ImeModePersistence-x64.msix
powershell -ExecutionPolicy Bypass -File packaging\msix\build.ps1 -Architecture arm64 -ExePath build-arm64/Release/ImeModePersistence.exe -OutDir msix-packages -OutName ImeModePersistence-arm64.msix
powershell -ExecutionPolicy Bypass -File packaging\msix\bundle.ps1 -Packages msix-packages/ImeModePersistence-x64.msix, msix-packages/ImeModePersistence-arm64.msix -OutFile dist/ImeModePersistence.msixbundle
```

The bundle version defaults to `AppxManifest.xml`'s `Version`, which is the one
Partner Center compares against previous submissions.

## Test locally (sideload)

The Store signs on upload, but to run the package on your own machine first,
sign it with a self-signed cert whose subject exactly equals the manifest's
`Identity/@Publisher`, trust that cert, then `Add-AppxPackage`.

## Submit

Upload the unsigned `.msixbundle` in Partner Center (it re-signs with the Store
certificate), fill in the listing (description, screenshots, the elevation
limitation note), and submit for certification. `release.yml` does this
automatically for stable tags.

Privacy policy URL for the listing (required for a full-trust app):
`https://github.com/mangokingTW/ImeModePersistence/blob/main/PRIVACY.md`

## Packaged-build behaviour

The app detects the MSIX context (`GetCurrentPackageFullName`) and adjusts:

- **Autostart** — the tray "start at logon" toggle is hidden; startup is the
  package's StartupTask, which the user enables in **Windows Settings > Apps >
  Startup** (an HKCU Run key would be virtualized to no effect). The manifest
  declares the task disabled by default.
- **Elevation** — **Restart as administrator** and the "needs administrator"
  notification are hidden, since a packaged app cannot elevate.
- Rules and settings live in the (virtualized) HKCU as usual and persist per
  package. The Inno installer is not used for this channel.

Optional later: an in-app toggle for the StartupTask via the
`Windows.ApplicationModel.StartupTask` WinRT API, so users need not open Settings.

Still to verify on a Windows machine after building the `.msix`: sideload it and
confirm autostart (via Settings), rules persistence, and the caret indicator all
work packaged.
