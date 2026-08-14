# Store screenshots

Version-controlled copies of the Microsoft Store listing screenshots — the
source of truth for what's uploaded to Partner Center.

These are **uploaded by hand** in Partner Center. The release workflow does *not*
upload listing images (it only submits the MSIX and sets each version's "What's
new"); this folder just keeps the screenshots under version control so they can
be reviewed, diffed, and re-uploaded when they change.

## Convention

- Named `NN-<short-desc>.<lang>.png` in display order, where `<lang>` is `en-US`
  or `zh-TW` — e.g. `01-app-language-bindings.en-US.png`. Drop the `.<lang>` for
  a language-neutral shot.
- Partner Center listings can carry their own screenshots per language: upload
  the matching-language shot to each listing. (If you'd rather, one set can be
  shared — adding a language reuses the default language's screenshots.)

These are usage screenshots (not icons), kept as-is at whatever size they were
captured.

See [`../../../docs/PACKAGING.md`](../../../docs/PACKAGING.md) for how the rest of
the Store listing (Description, "What's new") is managed.
