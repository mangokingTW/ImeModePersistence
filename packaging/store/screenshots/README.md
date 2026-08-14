# Store screenshots

Version-controlled copies of the Microsoft Store listing screenshots — the
source of truth for what's uploaded to Partner Center.

These are **uploaded by hand** in Partner Center. The release workflow does *not*
upload listing images (it only submits the MSIX and sets each version's "What's
new"); this folder just keeps the screenshots under version control so they can
be reviewed, diffed, and re-uploaded when they change.

## Convention

- Drop the PNG(s) here, named `NN-<short-desc>.png` in display order —
  e.g. `01-tray-menu.png`, `02-app-binding.png`, `03-caret-indicator.png`.
- One set serves every language: **screenshots are shared across listings**, so
  the `en-us` and `zh-tw` listings reuse these — no per-language upload needed.

## Microsoft Store requirements (desktop app)

- **PNG** only. At least **1**, up to **10** per listing.
- 16:9, from **1366×768** up to **3840×2160**.

See [`../../../docs/PACKAGING.md`](../../../docs/PACKAGING.md) for how the rest of
the Store listing (Description, "What's new") is managed.
