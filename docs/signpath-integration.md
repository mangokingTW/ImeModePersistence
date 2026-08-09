# SignPath release integration (activate after approval)

This is the change to make to `.github/workflows/release.yml` **once SignPath
Foundation has approved the project**. It is kept out of the live workflow until
then, because it needs secrets and IDs that only exist after approval — wiring it
early would fail every release.

## Prerequisites (from the SignPath dashboard, after approval)

- Organization id
- Project slug (e.g. `ImeModePersistence`)
- Signing policy slug (e.g. `release-signing`)
- An API token, stored as the repo secret `SIGNPATH_API_TOKEN`
  (Settings → Secrets and variables → Actions)

## Workflow change

Grant the release job no extra permissions beyond what it has. Between building
the executables and packaging the zips, submit each `.exe` for signing and
replace it with the signed copy, so the zips and installers contain signed
binaries:

```yaml
      - name: Sign the executables
        uses: SignPath/github-action-submit-signing-request@v1
        with:
          api-token: ${{ secrets.SIGNPATH_API_TOKEN }}
          organization-id: <ORG_ID>
          project-slug: ImeModePersistence
          signing-policy-slug: release-signing
          # Submit the built exes; the action downloads the signed versions back
          # to the same paths.
          artifact-configuration-slug: exe
          github-artifact-id: ${{ ... }}   # or use wait-for-completion with local paths
          wait-for-completion: true
          output-artifact-directory: build-signed
```

The exact inputs depend on whether you sign GitHub-artifact uploads or local
files; SignPath's action README documents both. The signing step must run
**before** `Package portable archives` and `Build installer`, so the zips and the
Inno Setup installers bundle the signed `.exe`. The provenance-attestation and
checksums steps then run over the signed outputs.

## After it is live

- Remove the "application pending" banner from
  [`docs/code-signing-policy.md`](code-signing-policy.md).
- Add the attribution and a link to the policy in the README.
- Update the wiki's "in development and unsigned" warning.
