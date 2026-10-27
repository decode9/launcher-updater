# Releasing

A release is a git tag `vX.Y.Z`. GitHub Actions builds it, tests it, signs it through SignPath
(once enabled) and publishes a GitHub Release with `launcher.exe` and `SHA256SUMS`. Servers
that ship the launcher download the executable from that Release and announce it in their
feed; installed launchers then update themselves.

## Cut a release

1. Make sure `main` is green (`build` workflow) and locally:
   `./build.sh && tests/run.sh && tools/check-denylist.sh`.
2. Set the new version in `VERSION` (`X.Y.Z`, nothing else). It feeds the executable's
   version resource and `LAUNCHER_VERSION`.
3. In `CHANGELOG.md`, move the `[Unreleased]` entries into a new `## [X.Y.Z] - YYYY-MM-DD`
   section. That section becomes the Release notes; the workflow fails without it.
4. Commit, then tag and push:

   ```sh
   git commit -am "Release X.Y.Z"
   git tag -a vX.Y.Z -m "vX.Y.Z"
   git push origin main vX.Y.Z
   ```

## What CI does (`.github/workflows/release.yml`)

Job `build` (GitHub-hosted `ubuntu-latest`, read-only token):

1. Fails if the tag is not `v` + `VERSION`.
2. Builds (`./build.sh`), runs the unit tests and the theme schema check, runs the denylist.
3. Uploads `build/launcher.exe` as a bare-file artifact (`archive: false`).
4. If the repository variable `SIGNPATH_ENABLED` is `true`: submits that artifact to SignPath
   and waits (up to an hour) for the signed file; checks that it carries a signature.
   Otherwise it uses the unsigned file and logs a warning.
5. Writes `SHA256SUMS` (`sha256sum` format) next to the chosen `launcher.exe`.

Job `release` (the only one with `contents: write`): verifies `SHA256SUMS`, extracts the
notes from `CHANGELOG.md` and runs `gh release create`. Unsigned releases are marked as
pre-releases and say so in the notes.

A tag that fails can be fixed and re-run from the Actions tab (SignPath accepts at most three
re-runs of the same build), or deleted and pushed again before anyone downloads it.

## Enable signing (after SignPath Foundation approves the project)

In SignPath:

1. Link the project to the predefined **GitHub.com** trusted build system.
2. Make this the project's **default** artifact configuration (the workflow does not pass an
   artifact configuration slug), with the bare executable as root:

   ```xml
   <artifact-configuration xmlns="http://signpath.io/artifact-configuration/v1">
     <pe-file path="launcher.exe" product-name="decode9 Launcher" company-name="decode9"
              original-filename="launcher.exe">
       <authenticode-sign description="decode9 Launcher"
                          description-url="https://github.com/decode9/launcher-updater" />
     </pe-file>
   </artifact-configuration>
   ```

   (Add `product-version` restrictions only if the configuration takes the version as a
   parameter; it changes with every release.)
3. Use the Foundation's release signing policy, with manual approval by the approvers listed
   in [`docs/code-signing-policy.md`](docs/code-signing-policy.md), and enable the
   GitHub-hosted runner policy (`required_github_hosted`).
4. Create a CI user / API token allowed to submit to that policy.

In GitHub, **Settings → Secrets and variables → Actions**:

| Kind | Name | Value |
|---|---|---|
| Secret | `SIGNPATH_API_TOKEN` | the SignPath API token |
| Variable | `SIGNPATH_ORGANIZATION_ID` | SignPath organization id (GUID) |
| Variable | `SIGNPATH_PROJECT_SLUG` | project slug, e.g. `launcher` |
| Variable | `SIGNPATH_SIGNING_POLICY_SLUG` | e.g. `release-signing` |
| Variable | `SIGNPATH_ENABLED` | `true` |

The next tag is signed. Approve the request in SignPath while the workflow waits.

## Verify a signature on Windows

```powershell
Get-AuthenticodeSignature .\launcher.exe | Format-List Status, SignerCertificate
(Get-Item .\launcher.exe).VersionInfo | Format-List ProductName, ProductVersion, CompanyName
Get-FileHash .\launcher.exe -Algorithm SHA256   # compare with SHA256SUMS
```

`Status` must be `Valid` and the signer `SignPath Foundation`. On Linux,
`osslsigncode verify launcher.exe` shows the same.

## If signing fails

- **The step fails or times out**: the Release is not created (the `release` job never runs).
  Open the signing request link in the step log. If it was denied or expired, re-run the
  workflow; if SignPath is down, re-run later.
- **Origin verification fails**: the build did not come from a GitHub-hosted runner, a pushed
  tag or the expected workflow file. Fix the workflow, tag a new patch version.
- **Metadata restriction fails**: the version resource (`src/platform/win32/res/launcher.rc`)
  no longer matches the artifact configuration. Fix one of them; do not loosen the
  configuration to accept anything.
- **A release is urgent and signing is blocked**: set `SIGNPATH_ENABLED` to `false`, re-run;
  the Release goes out unsigned as a pre-release. Launchers that already verify signatures
  will refuse to self-update to it, which is intended.
