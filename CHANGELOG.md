# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions follow
[Semantic Versioning](https://semver.org/). The release workflow publishes the section whose
heading matches the tag as the GitHub Release notes.

## [Unreleased]

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
