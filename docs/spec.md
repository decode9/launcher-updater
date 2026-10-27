# Launcher ↔ feed server protocol (v1)

This is the contract between the launcher and a game's feed server. The launcher is generic:
everything game-specific (files, theme, news, how to start the game) comes from the server or
from `launcher.json`.

## Transport
- HTTP/1.1 over TLS (1.2 minimum: Wine's Schannel only negotiates 1.2). `GET` only. One request
  per connection (`Connection: close`).
- The launcher pins the SHA-256 of the server certificate in DER form (64 lowercase hex digits,
  `fingerprint` in `launcher.json`). It does not validate the chain.
- Exception (`blobs_url`): the connection to that host does **not** use the pin; the system
  validates its certificate chain and host name. Integrity comes from the SHA-256 of every
  blob, which the launcher takes from the status and manifest fetched over the pinned
  connection.
- Default port in the examples: 5103 (any port works; it comes from `launcher.json`).
- Responses: `200` with `Content-Length`; `206` with `Content-Range` when the request has
  `Range: bytes=a-b` or `bytes=a-`; `404` (unknown route or hash); `416` (invalid range);
  `400` (malformed request). Request head at most 4 KB. No `Transfer-Encoding`.

## Routes
- `GET /v1/status` → JSON (`application/json; charset=utf-8`).
- `GET /v1/blob/<sha256>` → the bytes whose SHA-256 is exactly `<sha256>` (64 lowercase hex).
  Supports `Range`. Blobs may also be served from `blobs_url` in `launcher.json` (same path,
  `<blobs_url>/<sha256>`), to move downloads to other storage without changing the executable.

## `/v1/status`
```json
{
  "schema": 1,
  "server_time": "2026-10-27T15:00:00Z",
  "open": false,
  "opens_at": "2026-11-11T13:00:00Z",
  "news": [
    {"image": "<sha256>", "title": {"en": "Beta starts 11.11.2026", "es": "La beta empieza el 11.11.2026"}, "link": "https://…"}
  ],
  "theme":    {"sha256": "<sha256>", "size": 1234},
  "release":  {"version": "2026.10.27.1", "sha256": "<sha256>", "size": 5678},
  "launcher": {"version": "0.1.0", "sha256": "<sha256>", "size": 300000}
}
```
- Times are UTC ISO-8601 with `Z`. `opens_at` may be `null` (no date).
- `open` is already resolved by the server (a manual switch **or** `server_time ≥ opens_at`).
  The launcher computes the countdown as `opens_at − server_time` (plus its monotonic clock since
  the response), never with the PC clock.
- `theme`, `release` and `launcher` are blobs. `launcher` may be absent (no self-update).
- News titles: the user's language, else `en`, else the first one. Links must be `https://`.
- Unknown fields are ignored (both sides).

## Release manifest (JSON blob)
```json
{
  "schema": 1,
  "version": "2026.10.27.1",
  "ops": [
    {"op": "file", "path": "game.pak", "sha256": "…", "size": 1600000000,
     "chunks": [{"sha256": "…", "size": 8388608}, …]},
    {"op": "file", "path": "bin/plugin.dll", "sha256": "…", "size": 40000, "chunks": [{"sha256": "…", "size": 40000}]},
    {"op": "patch", "path": "game.pak", "base_sha256": "…", "base_size": 1600000000,
     "writes": [{"offset": 123456, "data": "a1b2c3d4"}],
     "append": {"sha256": "…", "size": 2500000},
     "result_sha256": "…", "result_size": 1602500000},
    {"op": "line", "path": "settings.ini", "prefix": "Server=", "line": "Server=play.example.com"}
  ]
}
```
- **Paths**: relative to the launcher folder, with `/`, no `..`, no drive or root, no `:`,
  no `\`, no empty or `.` component, no component ending in `.` or space, no reserved device
  name (`CON`, `NUL`, `COM1`…). The launcher rejects the whole manifest if one path breaks
  the rule. A manifest can never touch the launcher executable or `launcher.json`.
- **`file`**: a whole file, assembled from its `chunks` in order (each chunk is a blob,
  1 byte to 8 MiB; their sizes add up to `size`). `size` 0 with no chunks is an empty file. If
  it is already present with that SHA-256 (or is the base of a `patch` that the journal can
  restore), it is not downloaded. Downloads go to `<path>.part`; each chunk is verified before
  it counts, so an interrupted download resumes at the first incomplete chunk, and the file
  appears under its name only through an atomic rename once its SHA-256 matches.
- **`patch`**: on the base `base_sha256`/`base_size`: append the blob `append` at the end,
  then apply each of `writes` (lowercase hex bytes at `offset`, inside the base). Result:
  `result_sha256`/`result_size` (= `base_size` + `append.size`). `append` may be absent. If the
  file is not the base, the launcher refuses the release without touching it.
- **`line`**: in a text file (created if missing). Lines end in `\r\n`, `\n` or `\r`. The
  first line whose content, ignoring leading spaces, tabs, VT and FF, starts with `prefix`
  (exact bytes, case-sensitive) is replaced by `line`, keeping its own line ending; later
  matches stay as they are. If no line matches, `line` + `\r\n` is appended, preceded by
  `\r\n` when the file is not empty and does not end in `\n`. `prefix` and `line` cannot
  contain CR or LF.
- **Order**: ops apply in manifest order. A `patch` on a `path` requires that file at its base.
  A `file` op must be the first op on its path, a path has at most one `patch`, and nothing may
  follow the `patch` on the same path (the journal undoes exactly one patch per file); the
  launcher rejects a manifest that breaks this. Paths are compared without ASCII case.
- **Reserved paths**: besides the executable and `launcher.json`, `launcher.log` and anything
  under `.launcher/` (the launcher's state folder) are rejected.
- **Releases from the base**: when the version changes, the launcher undoes its journals (back
  to the base) and applies the new manifest in full.
- **Journal**: before appending, the base size is saved; before each in-place write, the old
  bytes (flushed to disk). On start, the journal of a half-finished install is undone first.
  Undo = truncate to the base size, then restore the old bytes in reverse order (idempotent).
- **Launcher state** (informative, not part of the wire contract): `.launcher/state.json` holds
  the installed release and the SHA-256/size of each managed file; a file whose entry matches
  and whose size on disk is unchanged is not hashed again. The same release with all sizes
  unchanged means there is nothing to do.

## Launcher flow
Informative (behaviour of this launcher, not part of the wire contract). Started without
arguments, the launcher opens its window with the cached theme (`.launcher/cache/theme.json`
and its blobs, each named by its SHA-256 and verified when loaded) or the built-in neutral
theme, in state `checking`. One worker thread then does every request in series:

1. `GET /v1/status`; the document is cached as `.launcher/cache/status.json`.
2. Self-update when `launcher.version` is greater than its own (below).
3. If `theme.sha256` differs from the theme on screen: download the theme, parse it, download
   every image and font it references (verified by SHA-256) into the cache, then show it. A
   theme that is rejected, needs a newer launcher or misses blobs is ignored (logged).
4. News images go to the cache too; an item whose image fails is shown without it.
5. `open: false` → state `locked`, countdown `opens_at − server_time`; the status is fetched
   again every 5 minutes and when the countdown reaches 0.
   `open: true` → state `ready`; if the release is not installed yet the status line says an
   update is available (Play installs first). The status is fetched again every 5 minutes.
6. **Play**: if `.launcher/state.json` does not already hold the release, install it (state
   `installing`, progress bar). Then start `launch.exe` with `launch.args` (`{server}`
   replaced), working folder = the launcher folder, and close the launcher. On failure the
   state goes back to `ready` with the error in the status line, so Play retries.
7. **Close / exit** stops an install in progress (it resumes next time) and waits for the
   worker before exiting. `minimize` and `open_url` (https only) are handled by the window.

**No server** (status cannot be fetched): if `.launcher/state.json` holds the complete release
named by the last cached status **and** that status said `open: true`, the launcher is `ready`
(offline) and Play starts the installed game without installing. Otherwise it is in state
`error`. Either way the status is retried every 30 seconds. (Local rule: playing offline only
makes sense when the game itself can reach its server; the launcher cannot know, so it allows
what the last confirmed state allowed.)

Extra status texts (theme keys, with built-in English fallbacks): `status.update`, `status.resume`,
`status.offline`, `status.connection`, `status.install_failed`, `status.no_space`,
`status.launch_failed`, `status.self_update`, `status.config`, `status.folder`.

## Self-update
When `launcher.version` in `/v1/status` is greater (X.Y.Z) than the running version:

1. Download the `launcher` blob; its size and SHA-256 must match the pinned status.
2. Write it as `<exe>.new` next to the executable (through `.part` and a rename).
3. Signer: if the running executable has an Authenticode signature that Windows accepts, the
   new one must also be accepted and have the same signer subject. If the running one is
   signed but does not verify on this machine, the new one must still be signed by the same
   subject. If the running one is unsigned (beta builds), the SHA-256 is the only check.
   (Wine implements `WinVerifyTrust` only partly; there, signed files may not verify.)
4. Start `<exe>.new --swap <pid>` and exit.
5. The new process waits for `<pid>` to exit (up to 60 s), renames itself over `<exe>`
   (40 attempts, 250 ms apart), starts `<exe> --skip-update` and exits. `--skip-update` limits
   self-update to one step per start, so a wrongly announced version cannot loop.
6. A normal start removes a leftover `<exe>.new`.

Any failure keeps the running version, which carries on and logs why. A theme whose
`min_launcher_version` is greater than the running version is only used after an update.

## `launcher.json` (next to the executable; not served)
```json
{
  "server": "play.example.com",
  "port": 5103,
  "fingerprint": "<64 hex>",
  "blobs_url": null,
  "launch": {"exe": "Game.exe", "args": ["0", "0", "{server}"]}
}
```
- `server`: host name or IPv4 (letters, digits, `.`, `-`). `port`: 1–65535.
- `blobs_url`: `null` or an `https://` URL.
- `launch.exe` must be a relative path inside the launcher folder (same rules as the
  manifest). `{server}` in `args` is replaced by `server`.

## Theme (JSON blob)
Full schema: [`theme.schema.json`](theme.schema.json); guide for theme authors: [`theming.md`](theming.md).
Summary:
```json
{
  "schema_version": 1,
  "min_launcher_version": "0.1.0",
  "window": {"width": 960, "height": 600, "background": {"image": "<sha256>", "color": "bg"}, "radius": 12, "draggable": true, "title": {"en": "…", "es": "…"}},
  "tokens": {"bg": "#101418FF", "primary": "#C89B3CFF", "text": "#F0E6D2FF", "muted": "#8A8A8AFF", "danger": "#D04040FF"},
  "fonts":  {"body": {"family": "Segoe UI", "size": 14, "weight": 400}, "title": {"file": "<sha256>", "family": "…", "size": 28, "weight": 700}},
  "strings": {"en": {"play": "Play", "countdown": "Opens in {days}d {hours}:{minutes}:{seconds}"}, "es": {"play": "Jugar"}},
  "elements": [
    {"type": "image", "rect": [40, 30, 300, 100], "image": "<sha256>"},
    {"type": "news", "rect": [40, 150, 560, 300], "interval": 8},
    {"type": "countdown", "rect": [620, 420, 300, 30], "font": "title", "color": "primary", "text": "countdown"},
    {"type": "status", "rect": [40, 470, 560, 20], "font": "body", "color": "muted"},
    {"type": "progress", "rect": [40, 495, 560, 14], "style": {"normal": {"fill": "primary", "track": "#00000080", "radius": 7}}},
    {"type": "button", "rect": [760, 520, 160, 48], "action": "play", "text": "play",
     "style": {"normal": {"fill": "primary", "color": "bg", "radius": 8}, "hover": {"fill": "#E0B04CFF"}, "pressed": {}, "disabled": {"fill": "muted"}}},
    {"type": "button", "rect": [920, 8, 32, 32], "action": "exit", "text": "×"}
  ]
}
```
- Colors: a token name or `#RRGGBBAA`. Texts: a key of `strings` (Windows UI language; if
  missing, `en`; if missing, the literal key). Variables: `{days} {hours} {minutes} {seconds}
  {percent} {version} {done} {total}`.
- Style states inherit from `normal`. Unknown types and fields are ignored. If
  `min_launcher_version` is greater than the executable's version, the launcher updates itself
  before using the theme.
- Button actions: `play` (install/update if needed, then launch), `exit`, `minimize`,
  `open_url` (with `url`, `https://` only).
