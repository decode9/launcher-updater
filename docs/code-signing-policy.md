# Code signing policy

Free code signing provided by [SignPath.io](https://about.signpath.io/), certificate by
[SignPath Foundation](https://signpath.org/).

> Status: the project has not been accepted by SignPath Foundation yet. Until it is, releases
> are published unsigned (marked as pre-releases) and this policy describes how signing will
> work once enabled.

## What is signed

- Only `launcher.exe`, built from this repository's source by the `release` workflow
  (`.github/workflows/release.yml`) on GitHub-hosted runners, from a `vX.Y.Z` tag.
- No third-party binaries are signed. The executable contains no third-party code: it links
  only against Windows system libraries.
- Every signed file carries fixed metadata: product name `decode9 Launcher`, company
  `decode9`, original file name `launcher.exe`, and the product version from `VERSION`.

## Team roles

| Role | Members |
|---|---|
| Committers and reviewers | [decode9](https://github.com/decode9) |
| Approvers | [decode9](https://github.com/decode9) |

- **Committers** may change the source directly; changes from anyone else are merged only
  after a committer reviews them in a pull request.
- **Approvers** approve each signing request by hand in SignPath, after checking that it comes
  from the expected tag and workflow run.
- Every team member uses multi-factor authentication on GitHub and SignPath.

## Privacy policy

This program will not transfer any information to other networked systems unless
specifically requested by the user or the person installing or operating it.

In detail:

- The launcher talks only to the feed server written in `launcher.json` by whoever ships the
  game (and, if set, to the `blobs_url` download host). It sends HTTPS requests for the status,
  news, theme, release manifest and files. These requests carry no personal data, no
  identifiers and no telemetry; the server sees the usual connection data (IP address, time).
- It writes a log (`launcher.log`) and its state (`.launcher/`) only in its own folder. Nothing
  is uploaded.
- The privacy policy of the feed server is that of whoever operates it.

## Downloaded content

The launcher is an installer and updater: it **downloads files from the server configured in
`launcher.json` and starts the program that configuration names.** It protects that content
as follows:

- The feed server is authenticated by a pinned certificate fingerprint; a different
  certificate aborts the connection before any request is sent.
- Every file is requested and verified by its SHA-256, taken from the pinned release manifest.
  A file only takes its final name, by an atomic rename, after it is verified.
- Every path is confined to the launcher's folder; one bad path rejects the whole manifest.
- A new `launcher.exe` is accepted only after its SHA-256 matches the manifest; once releases
  are signed, it must also carry a valid Authenticode signature from the same publisher.
- The launcher does not change system settings, does not need administrator rights
  (`asInvoker`) and installs nothing outside its folder: deleting the folder uninstalls it.

## Reporting

Report a signed binary that misbehaves, or a suspected policy violation, by opening an issue in
this repository or by writing to SignPath Foundation (see [signpath.org](https://signpath.org/)).
