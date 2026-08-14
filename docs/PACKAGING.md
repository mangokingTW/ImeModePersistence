# Packaging & Releasing

How ImeModePersistence is built and published. Everything is driven by pushing a
`vX.Y.Z` tag — [`.github/workflows/release.yml`](../.github/workflows/release.yml)
does the rest.

## Distribution channels

| Channel | Identifier | Updated by |
|---|---|---|
| GitHub Releases | `mangokingTW/ImeModePersistence` | `release.yml` on tag push |
| Microsoft Store | Store ID `9P05QQZ2P5XC` | `release.yml` (stable tags only) |
| Chocolatey | `imemodepersistence` | `chocolatey.yml` on release published |
| Scoop | `mango/ImeModePersistence` | manifest in [`packaging/scoop`](../packaging/scoop), updated separately |
| winget | `mangokingTW.ImeModePersistence` | manifest in [`packaging/winget`](../packaging/winget), updated separately |

Release assets: two installers (`-setup-admin.exe`, `-setup-user.exe`), portable
`-x64.zip` / `-x86.zip`, the Store `.msix`, `SHA256SUMS.txt`, and the SLSA
provenance bundle (`multiple.intoto.jsonl`).

## Cutting a release

1. **Bump the version in three places** (they must agree):
   - `CMakeLists.txt` — `project(ImeModePersistence VERSION X.Y.Z ...)` (the
     single source of truth, stamped into `VERSIONINFO`).
   - `packaging/msix/AppxManifest.xml` — `Version="X.Y.Z.0"`. **Keep the 4th
     field `0`** (the Store rejects a non-zero revision), and it **must be
     higher than the last version submitted to the Store**, or the Store rejects
     the resubmission.
   - `CHANGELOG.md` — add a `## vX.Y.Z` section (see [CHANGELOG format](#changelog-format)).
2. Open a PR and merge to `main` (branch-protected; required checks: `version
   bump`, `x64`, `x86`, `installer`).
3. Tag `main` and push:
   ```sh
   git tag -a vX.Y.Z -m vX.Y.Z && git push origin vX.Y.Z
   ```
   That triggers `release.yml`.

For a **test build**, push `vX.Y.Z-beta.N` (or `-rc.N`): it publishes as a GitHub
pre-release, never becomes "Latest", the Store step is skipped, and `scoop
install mango/ImeModePersistence-beta` tracks it.

> The `version bump` check only rejects a version that moves *backward*; ordinary
> PRs need no bump. The tag must be `MAJOR.MINOR.PATCH`, optionally with a
> `-beta.N` / `-alpha.N` / `-rc.N` suffix.

## What `release.yml` does

1. **Derive version** from the tag (`FILEVER` = full, `VERSION` = numeric).
2. **Build** x64 + x86 (CMake/MSVC Release).
3. **Build installer** (Inno Setup, `installer/ImeModePersistence.iss`).
4. **Package** portable zips; **build the MSIX** (`packaging/msix/build.ps1`).
5. **Checksums** + **build provenance attestation**.
6. **Create the GitHub release** — notes from the CHANGELOG section plus a fixed
   install/verify footer (or auto-generated notes if the tag has no section). It
   refuses to publish unless every expected asset is present.
7. **Microsoft Store submit** — stable tags only, and a no-op unless the
   `PARTNER_CENTER_*` secrets exist. Runs *after* the GitHub release, so a Store
   hiccup never blocks the release.

## CHANGELOG format

Each version is a `## vX.Y.Z` heading followed by **bilingual highlights** —
English first, then a paragraph beginning `繁體中文:`:

```markdown
## v1.0.4

Overview of what changed / what the app does…

- English bullet.

繁體中文:中文說明……

- 中文條目。
```

The section drives two different outputs:

- **GitHub release note** = the highlights **+** a constant install/verify footer
  (scoop/winget/choco commands, `gh attestation verify`). The footer is
  GitHub-only — it must never reach the Store listing.
- **Store "What's new"** = the highlights **only**, and **split by language**: the
  workflow cuts the section at the `繁體中文:` marker into `storenotes.en.md` /
  `storenotes.zh.md`, then assigns per listing — `en-us` gets English, `zh-tw`
  gets Chinese. (A single-listing app would get the bilingual note instead.)

Keep the highlights **≤ 1500 characters per language** (the Store's plain-text
release-notes cap). If a tag has no CHANGELOG section, the GitHub note is
auto-generated and the Store "What's new" is left as the previous version's.

## Microsoft Store

MSIX identity: `Name="MangoYen.IMEModePersistence"`,
`Publisher="CN=B64E145E-DB3F-473D-9BA6-BDF6CF2E8081"` — matches the Partner
Center reservation. The Store re-signs the package on ingestion, so the `.msix`
produced by CI is unsigned. The Store (MSIX) build **cannot elevate**, so for
targets that need administrator rights it points the user to the desktop build.

### One-time setup

- **Entra ID app** (free — Entra Free tier, no Azure subscription). Associate the
  Entra tenant with Partner Center (*Account settings → Organization profile →
  Tenants → Associate*); that makes the "Microsoft Entra applications" tab appear
  under *User management*. Give the app the **Manager (Windows)** role.
- **First submission must be manual** and fully **published** in Partner Center
  (age ratings etc. can't be set by the API, and the API can only create the
  *next* submission after one is live).
- **Secrets** (repo → Settings → Secrets): `PARTNER_CENTER_TENANT_ID`,
  `PARTNER_CENTER_CLIENT_ID`, `PARTNER_CENTER_CLIENT_SECRET`,
  `PARTNER_CENTER_SELLER_ID`. The **Seller ID must be the numeric value**
  (*Account settings → Identifiers*) — not the Publisher GUID, CN, or Store ID.
- Verify the wiring anytime with the **`store-check.yml`** workflow
  (`workflow_dispatch`): it runs `msstore apps list` — no submission.

### How the auto-submit works

`msstore publish` alone clones the previous submission's release notes and never
updates them, so the Store step drives the submission explicitly:

```
msstore reconfigure …                       # auth from the secrets
msstore submission delete <id> --no-confirm # cancel any pending one (best-effort)
msstore publish <msix> -id <id> -nc         # upload the package into a draft, no commit
msstore submission get <id>                 # read the draft JSON
  → set listings.<lang>.baseListing.releaseNotes per language
msstore submission update <id> <json>       # full-object replace (keeps the package)
msstore submission publish <id>             # commit → server-side certification
```

Setting the notes is **best-effort**: if any of the get/update steps fail, the
package still commits (with the previous notes) — it must never block the
release. A 30-minute step timeout guards a known msstore commit hang.

### Gotchas (learned the hard way)

| Symptom | Cause / fix |
|---|---|
| `RetrieveSellerId` `FormatException` | `PARTNER_CENTER_SELLER_ID` isn't numeric. Use the numeric Seller ID. |
| `Ingestion API can only update, delete, and commit submissions that are created through the API` | A submission is pending that was created in the Partner Center **website**. Publish or discard it in the portal; the API can't supersede a portal-created submission. |
| `Failed to read input in non-interactive mode` at `submission delete` | Needs `--no-confirm` (it prompts y/n otherwise). |
| `Invalid Unicode escape sequence: \u` when parsing notes | `msstore submission get` prints JSON via AnsiConsole, which hard-wraps to ~80 cols and splits `\uXXXX` escapes. The workflow rejoins the physical lines (`-join ''`) before parsing — any listing with non-ASCII text hits this. |
| Store rejects the package | The `.msix` version equals a version already submitted. Bump `AppxManifest.xml` `Version` (4th field stays `0`). |
| "What's new" didn't change | The tag had no `## vX.Y.Z` CHANGELOG section, so `storenotes*.md` weren't written and the notes fell back to the previous submission's. |

Only one submission can be in flight per app at a time; a newer release
supersedes the pending one.

## See also

- [`packaging/msix/README.md`](../packaging/msix/README.md) — building/side-loading the MSIX locally.
- [`docs/design.md`](design.md) — design trade-offs and rejected approaches.
