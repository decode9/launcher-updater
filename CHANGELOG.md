# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions follow
[Semantic Versioning](https://semver.org/). The release workflow publishes the section whose
heading matches the tag as the GitHub Release notes.

## [Unreleased]

## [0.2.0] - 2026-10-29

### Security

- DLLs are only loaded from System32 (`DependentLoadFlags` in a load configuration directory, and
  `SetDefaultDllDirectories` at start): a DLL planted next to `launcher.exe` is not loaded.
- Self-update compares the signer's full subject and issuer names, not only the common name, and
  checks revocation of the whole chain online; when the status cannot be checked the update is
  refused and the current version keeps running.

### Added

- Single instance per folder: a second window brings the running one to the front;
  `--install` exits with code `4` while another launcher works in the folder.
- The release manifest is refused when a path ends in `.part`, a path is both a file and a folder
  (`a` and `a/b`), it has more than 1024 `patch` ops, or its sizes add up past 2^64.
- Window scales with the system DPI (theme coordinates are logical pixels at 96 DPI) and is
  shrunk to fit the work area, so the exit button is always reachable.
- "No version has been published yet" state (theme key `status.no_release`) with Play disabled;
  `/v1/status` may omit `release` or set it to `null`.

### Fixed

- A release that renames many files no longer fails with a full state: files of the previous
  release that the new one does not mention are deleted at the start of the install. Only files
  the launcher delivered whole (`file` ops) are deleted; files it only edited are kept. The
  `.part` of a download that never finished is deleted too, and folders left empty are removed,
  so a later release can put a file where a folder was.
- A launcher refused by the single-instance lock can still write to `launcher.log`.
- A `state.json` that exists but cannot be read (held by an antivirus) stops the install without
  writing anything, instead of being replaced by an empty state; a corrupt one is kept as
  `state.json.bad`.
- Undoing a patch journal hashes the file; a file edited outside the launcher is no longer taken
  as the base.
- Self-update no longer loops when the announced blob is the running executable (wrong version
  published); the error is logged once.
- Read-only files and a read-only `launcher.exe` are replaced and deleted (the read-only
  attribute is cleared after "access denied"); starting the new executable is retried three
  times in case an antivirus holds it.
- A click on Play while the launcher polls the server is no longer lost, and Play asks the
  server for a fresh status before installing or starting the game.
- When the countdown has ended but the server is still closed, polls back off from 5 to 60
  seconds with a random spread instead of every 5 seconds.
- News images are reloaded only when the news list changes, and the carousel keeps its place.
- Closing the window stops every download (theme, news, manifest, self-update, install) and the
  process ends within about 3 seconds even if a connection hangs.

## [0.1.1] - 2026-10-27

### Security

- A blob response larger than the requested chunk is refused before it is written to disk;
  blobs of unknown size (theme images, fonts) are capped at 8 MiB.
- Manifest paths with 8.3 short-name aliases (`~` + digit) and `<exe>.new` are rejected.
- Self-update fails closed for signed builds: if the running executable's signature does not
  verify, no update is installed; the new executable must verify and have the same signer.

### Fixed

- A user cancelling an install is logged as information, not as an error.
- The status log no longer prints a countdown when the server is already open.
- An interrupted install shows "Play resumes it" (theme key `status.resume`) instead of
  "an update is available".

## [0.1.0] - 2026-10-27

### Added

- `launcher.json` configuration and `/v1/status` client (`--status`).
- Release installer (`--install`): content-addressed blobs verified by SHA-256, resumable
  chunked downloads, crash-safe patch journals, path confinement.
- TLS (Schannel) with certificate pinning; optional `blobs_url` for a CDN.
- Themed window with an embedded neutral theme.
- Windows resources: application icon, version information and manifest (`asInvoker`,
  Windows 10/11, system DPI aware).
- Release workflow: tag `vX.Y.Z` builds, tests and publishes `launcher.exe` with `SHA256SUMS`,
  signed through SignPath once enabled.
