# SignPath Foundation application (template, not submitted)

Fill this in when applying at <https://signpath.org/apply>. The form's exact fields may change;
check the [conditions](https://signpath.org/terms) first.

## Before applying (checklist)

- [ ] Repository public on GitHub (`decode9/launcher-updater`), MIT license in `LICENSE`.
- [ ] At least one release published from the `release` workflow (unsigned pre-release is fine:
      the conditions ask for software "already released in the form to be signed").
- [ ] README describes what the launcher does and links the
      [code signing policy](code-signing-policy.md) ("Code signing policy" on the home page
      and in the release notes).
- [ ] MFA enabled on GitHub (and later SignPath) for every team member.
- [ ] No proprietary code; no game-specific content (`tools/check-denylist.sh`).

## Answers

| Field | Answer |
|---|---|
| Project name | decode9 Launcher |
| Repository URL | https://github.com/decode9/launcher-updater |
| Homepage / download page | https://github.com/decode9/launcher-updater/releases |
| License | MIT (OSI approved) |
| Description | A small generic Windows launcher, written in C without a C runtime, that installs, updates and starts a game from a feed server. Downloads are verified by SHA-256 against a manifest served over TLS with a pinned certificate. |
| What is signed | One file: `launcher.exe` (Windows PE, i686) |
| Build system | GitHub Actions, GitHub-hosted `ubuntu-latest`, `.github/workflows/release.yml`, triggered by `vX.Y.Z` tags |
| Toolchain | clang, lld-link, llvm-rc, llvm-dlltool (no MSVC, no Windows SDK) |
| Release cadence | (fill in: e.g. a few releases per year) |
| Maintainers / roles | decode9: author, reviewer, approver (see `docs/code-signing-policy.md`) |
| Contact e-mail | (fill in) |
| Code signing policy URL | https://github.com/decode9/launcher-updater/blob/main/docs/code-signing-policy.md |
| Privacy | No telemetry; network traffic only to the server configured by whoever ships the game |
| Downloads/executes other files? | Yes: it is an installer/updater; it downloads files from the configured server, verifies each by SHA-256 and starts the configured program. Disclosed in the policy. |
| Number of users | (fill in) |

## After approval

Follow "Enable signing" in [`RELEASING.md`](../RELEASING.md).
