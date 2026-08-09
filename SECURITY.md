# Security Policy

## Reporting a vulnerability

Please report suspected vulnerabilities privately, not as a public issue.

Use GitHub's private reporting on this repository — the **Security** tab →
**Report a vulnerability**
(<https://github.com/mangokingTW/ImeModePersistence/security/advisories/new>).
That keeps the report visible only to the maintainer until a fix is out.

Expect an acknowledgement within a few days. This is a personal open-source
project maintained in spare time, so there is no paid support or formal SLA, but
security reports are taken seriously and prioritised over feature work.

There is no bounty programme.

## Supported versions

Only the [latest release](https://github.com/mangokingTW/ImeModePersistence/releases/latest)
is supported. Fixes ship in a new release rather than as patches to older ones.

## Verifying a download

Every release artifact carries build provenance, signed by the release workflow.
It proves a file was built from this source by this repository's GitHub Actions
and was not altered afterward:

```
gh attestation verify <file> --repo mangokingTW/ImeModePersistence
```

Checksums are published alongside each release as `SHA256SUMS.txt`.

## What the program can and cannot do

The utility is unsigned, and its optional logon task trips Microsoft Defender's
behaviour heuristic (`Behavior:Win32/Persistence.A!ml`) — a false positive on an
unsigned program that schedules itself at logon, which any autostart feature must
do. The source is small and auditable, and the capability boundary is deliberate:

- **No network.** The binary links no networking library at all
  (`ws2_32`/`wininet`/`winhttp`/`urlmon` are absent), so "transfers no
  information" is structural rather than a promise.
- **No injection, hooks, or keylogging.** It contains none of
  `WriteProcessMemory`, `CreateRemoteThread`, `VirtualAllocEx`,
  `SetWindowsHookEx`, `GetAsyncKeyState`, or `SendInput`.
- **Narrow writes.** Only under `HKCU\Software\ImeModePersistence` and the
  `Run` key, and only to `%LocalAppData%\ImeModePersistence\log.txt`.
- **Explicit elevation only.** The manifest is `asInvoker`; elevation happens
  solely through *Restart as administrator* or the admin installer's UAC prompt.

Independent checks run on every change and are public: CodeQL and MSVC
`/analyze` (results in the **Security** tab), and OpenSSF Scorecard. A full
account is in [`docs/design.md`](docs/design.md) under "Verifying it".
