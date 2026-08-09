# Code signing policy

> **Status: application pending.** Releases are **not yet code-signed**. This page
> documents the arrangement that takes effect once SignPath Foundation approves
> the project; until then, verify downloads with the published `SHA256SUMS.txt`
> and build provenance (`gh attestation verify <file> --repo mangokingTW/ImeModePersistence`).

Free code signing provided by [SignPath.io](https://about.signpath.io), certificate by [SignPath Foundation](https://signpath.org).

## Roles

This is a single-maintainer project, so one person holds every role:

- **Committers** (may modify source without additional review): [@mangokingTW](https://github.com/mangokingTW)
- **Reviewers** (approve changes from non-committers): [@mangokingTW](https://github.com/mangokingTW)
- **Approvers** (authorise each signing request): [@mangokingTW](https://github.com/mangokingTW)

## Privacy

This program will not transfer any information to other networked systems unless specifically requested by the user or the person installing or operating it.

This is not merely a policy statement: the program links no networking library at
all (no `ws2_32`/`wininet`/`winhttp`/`urlmon`), so it has no means to open a
network connection. See [`docs/design.md`](design.md) under "Verifying it".

## How signing works, once active

Releases are built by GitHub Actions from the tagged commit (see
[`.github/workflows/release.yml`](../.github/workflows/release.yml)). After
approval, the release workflow submits the built artifacts to SignPath for
signing and publishes the signed binaries; every signing request is approved
manually. Signed binaries carry product-name and version metadata, and the same
build provenance attestation as today.
