# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions follow
[Semantic Versioning](https://semver.org/). The release workflow publishes the section whose
heading matches the tag as the GitHub Release notes.

## [Unreleased]

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
