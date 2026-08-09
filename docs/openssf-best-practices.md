# OpenSSF Best Practices badge — submission notes

The badge is a **self-assessment** and must be submitted with your own account;
it cannot be automated. This file is the paste-ready set of answers. Once the
badge is awarded, Scorecard's `CII-Best-Practices` check picks it up.

## How to submit

1. Sign in at <https://www.bestpractices.dev> with GitHub.
2. *Add a project* and enter
   `https://github.com/mangokingTW/ImeModePersistence`. Many fields autofill from
   the repository.
3. Work through the questionnaire using the answers below.
4. When it reaches "passing", tell me the project id from the badge URL
   (`bestpractices.dev/projects/<id>`) and I will add the badge to the README.

## Answers (passing level)

Almost everything is **Met** and the repository is the evidence:

- **License** — MIT (`LICENSE`), OSI-approved.
- **Basic + interface docs** — README, the
  [wiki](https://github.com/mangokingTW/ImeModePersistence/wiki), `docs/design.md`.
- **HTTPS site, English, issue tracker** — the GitHub repository.
- **Public version control, unique semantic versioning, release notes** —
  `CMakeLists.txt` is the single source of truth; CI blocks a PR that does not
  bump it, and each release has generated notes.
- **Bug reporting** — GitHub Issues (see `CONTRIBUTING.md`).
- **Vulnerability reporting** — `SECURITY.md`, private GitHub advisories.
- **Contribution guide** — `CONTRIBUTING.md` (build, test, version-bump, test
  policy, style).
- **Build + automated tests + test policy** — CMake, CTest suites in `tests/`,
  run on every PR; the policy that new behaviour ships with a test is stated in
  `CONTRIBUTING.md`.
- **Compiler warnings** — `/W4 /permissive-` with warnings-as-error in CI.
- **Static analysis** — CodeQL and MSVC `/analyze`, results in the Security tab.
- **Dynamic analysis** — ClusterFuzzLite fuzzes the marker parser with
  AddressSanitizer on every PR.
- **Secured delivery** — releases over HTTPS, each artifact carrying build
  provenance (`gh attestation verify`).
- **No known unpatched vulnerabilities** — Scorecard `Vulnerabilities` is clean.

Fields to mark **N/A**, with the note *"performs no cryptography and opens no
network connections; the binary links no networking library"*:

- all cryptography questions (`crypto_*`),
- anything about network/TLS configuration of a running service.
