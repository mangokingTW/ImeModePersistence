# Chocolatey package

Community-repo package for [Chocolatey](https://community.chocolatey.org). It
downloads the official machine-wide installer from GitHub Releases and verifies
its SHA256; Chocolatey's auto-uninstaller removes it via the Add/Remove Programs
entry, so no uninstall script is needed.

## Publishing a new version

1. Update `imemodepersistence.nuspec` `<version>` and `<releaseNotes>`.
2. In `tools/chocolateyInstall.ps1`, bump `$version` and set `$checksum` to the
   new `setup-admin.exe` SHA256 (from the release's `SHA256SUMS.txt`, uppercased).
   Update `tools/VERIFICATION.txt` to match.
3. Pack and push (needs a chocolatey.org account + API key):

   ```powershell
   choco pack packaging\chocolatey\imemodepersistence.nuspec
   choco apikey --key <YOUR_API_KEY> --source https://push.chocolatey.org/
   choco push imemodepersistence.1.0.0.nupkg --source https://push.chocolatey.org/
   ```

4. First submission goes through community moderation; later versions of an
   approved package are usually auto-verified.

## Testing locally

```powershell
choco pack packaging\chocolatey\imemodepersistence.nuspec
choco install imemodepersistence --source . --yes
choco uninstall imemodepersistence --yes
```
