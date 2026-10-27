# launcher

A small, generic Windows launcher that installs, updates and starts a game from a feed server.
Written in C11 with no C runtime and no Windows SDK: one ~120 KB `launcher.exe` (i686, GUI
subsystem) that only imports system DLLs (`kernel32`, `user32`, `gdi32`, `gdiplus`, `shell32`,
`shlwapi`, `ws2_32`, `secur32`, `crypt32`, `wintrust`).

Nothing in the executable is game-specific. The game is described by:

- `launcher.json`, shipped next to the executable (server, port, certificate pin, how to start
  the game);
- the feed server, which serves the status, news, theme, release manifest and file blobs.

The protocol is in [`docs/spec.md`](docs/spec.md).

**Status: 0.1.0.** Started without arguments it opens the themed window: it fetches
`/v1/status`, the server's theme and news (cached in `.launcher/cache/`), shows a countdown
while the server is closed, and **Play** installs or updates the release (resumable,
crash-safe) with a progress bar and starts the game. It replaces itself when the server
announces a newer launcher. Everything is logged to `launcher.log`.

| Command | What |
|---|---|
| `launcher.exe` | the window (flow in [`docs/spec.md`](docs/spec.md#launcher-flow)) |
| `launcher.exe --status` | fetch the status, log it, exit |
| `launcher.exe --install` | install or update the release without a window, exit |
| `launcher.exe --theme-preview [folder]` | show a theme without a server ([`docs/theming.md`](docs/theming.md)) |
| `launcher.exe --swap <pid>` | internal: second half of self-update |

## Layout

| Path | What |
|---|---|
| `src/core/` | Portable C, no OS calls: JSON parser, SHA-256, `launcher.json`, `/v1/status`, theme and release manifest models, path confinement, version compare, HTTP message parsing, and the install engine (`install.c` with `state`, `journal`, `lineop`) over injected file and network ports. Unit tested on Linux, with fault injection. |
| `src/platform/win32/` | Win32 only: hand-written declarations (`win32.h`, `.def` import lists), runtime helpers (`rt.c`), files, TLS (Schannel) with certificate pinning, HTTP/1.1 client, the install engine's ports (`install_port.c`), the window (`ui/`), threads, processes and the Authenticode check (`trust.c`). |
| `src/app/` | Orchestration: the window's worker (`window.c`), the cache (`cache.c`), self-update (`selfupdate.c`), `--status`/`--install` (`app.c`), logging. |
| `src/main.c` | Entry point. |
| `tests/` | Host unit tests for `src/core` (`tests/run.sh`). |
| `examples/dev-server/` | Python feed server for local testing with a fictional "Example Game". |
| `examples/theme-neutral/`, `examples/theme-demo/` | The built-in colour-only theme and a demo theme with images; see [`docs/theming.md`](docs/theming.md). |
| `tools/` | Repository checks, `embed.py` (built-in theme into C), `make-demo-theme.py`. |

Dependencies point inwards: `core` ← `platform` ← `app` ← `main.c`. `core` never includes a
platform header.

## Build

Needs `clang`, `lld` and `llvm` (Debian/Ubuntu: `apt install clang lld llvm`).

```sh
./build.sh        # -> build/launcher.exe, version from VERSION
tests/run.sh      # core unit tests with the host compiler (ASan/UBSan when available)
```

No C runtime means: large buffers are static (there is no `__chkstk`, so a stack frame over
4 KiB fails the link), no 64-bit division (no `__aulldiv`), and `rt.c` provides `memset`,
`memcpy`, `memmove` and `memcmp`.

## Try it with the dev server

```sh
cd examples/dev-server
./make-cert.sh /tmp/feed            # prints the certificate pin
python3 server.py --cert /tmp/feed/cert.pem --key /tmp/feed/key.pem --port 5103 &

mkdir -p /tmp/game && cp ../../build/launcher.exe /tmp/game/
cat > /tmp/game/launcher.json <<JSON
{"server": "127.0.0.1", "port": 5103, "fingerprint": "$(cat /tmp/feed/fingerprint.txt)",
 "blobs_url": null, "launch": {"exe": "Game.exe", "args": ["{server}"]}}
JSON
(cd /tmp/game && wine launcher.exe --status; cat launcher.log)
```

Run `wine launcher.exe` in the same folder for the window. `examples/dev-server/fake-game.c`
is a stand-in game (it writes its command line to `fake-game.txt`); `server.py --rate 4096`
slows blob downloads to 4 MiB/s so the progress bar can be watched; `--open` opens the server
regardless of `opens_at`.

Exit codes: `0` status read, `1` configuration error, `2` network or server error.

Install a release into an empty folder with `launcher.exe --install` (exit code `3` = install
error). `make-release.py` generates a 22 MB example release (a 20 MiB file in 8 MiB chunks, a
patch and a line op) into `example-root/generated/` and rewrites `release.json` (restart
`server.py` afterwards); it prints the SHA-256 each file must end with:

```sh
python3 examples/dev-server/make-release.py
(cd /tmp/game && wine launcher.exe --install; tail launcher.log; sha256sum data/big.pak)
```

Kill the launcher mid-download and run it again: it re-verifies the complete chunks in
`data/big.pak.part` and downloads only the rest.

## Install engine

- **State** lives in `.launcher/` next to the executable: `state.json` (installed release,
  SHA-256 and size of each managed file), patch journals `j-<id>.jnl`, temporary files.
  Downloads go to `<path>.part` next to their target, so the final rename is atomic.
- **Crash safety**: a file appears under its name only by an atomic rename of verified bytes;
  a patched file is touched only after its journal is flushed and listed in `state.json`;
  undoing a journal is idempotent. `tests/test_install.c` kills the engine at every file
  operation and every download in turn and checks that the next run always finishes.
- **Releases from the base**: before a new release, every patch journal is undone, then the new
  manifest is applied in full.
- **For a UI**: `install_run(ctx, manifest, &result)` is synchronous; call it on a worker
  thread. `ctx.progress(user, {phase, path, done, total})` runs on that thread (about once per
  MiB; post it to the window). Set `*ctx.cancel` from any thread to stop; the next run
  resumes. `install_is_current(ctx, version)` answers "nothing to do" from `state.json` alone,
  before downloading the manifest. The HTTP client is not reentrant: no other request while
  an install runs.

## Self-update

When `/v1/status` has a `launcher` version greater than its own, the launcher downloads that
blob (checked by size and SHA-256 from the pinned status), checks its signer, writes
`launcher.exe.new`, starts `launcher.exe.new --swap <pid>` and exits. The new process waits
for the old one to exit, renames itself over `launcher.exe` (with retries), starts it again and
exits. Details and the signer rule: [`docs/spec.md`](docs/spec.md#self-update).

## Security

- **Pinned certificate.** The server is authenticated only by the SHA-256 of its DER
  certificate (`fingerprint`). Chains and system trust stores are not used; a wrong
  certificate aborts the connection before any request is sent.
- **Content addressed.** Every blob is requested and verified by its SHA-256.
- **Confined paths.** Every path from `launcher.json` or a manifest must be relative, use `/`,
  and stay inside the launcher folder: no root, drive, `:`, `\`, `..`, `.` or empty component,
  no trailing dot/space, no device names. One bad path rejects the whole manifest.
- **Strict parsing.** The JSON parser is strict RFC 8259, iterative (bounded depth) and works in
  caller-provided memory; integers are exact 64-bit; HTTP bodies need `Content-Length`.
- **Bounded waits.** Connect, send and receive time out.
- **`blobs_url`.** Blobs may come from another https host (a CDN). That connection uses the
  system's chain validation instead of the pin; each blob is still verified against the
  SHA-256 from the pinned manifest.

## License

MIT, see [`LICENSE`](LICENSE).

## Releases and signing

- Releases come from `vX.Y.Z` tags: GitHub Actions builds `launcher.exe`, tests it and publishes
  it with `SHA256SUMS` on the [Releases](https://github.com/decode9/launcher-updater/releases)
  page. How to cut one: [`RELEASING.md`](RELEASING.md); changes: [`CHANGELOG.md`](CHANGELOG.md).
- The executable carries a version resource (product `decode9 Launcher`, version from
  `VERSION`), an icon and a manifest (`asInvoker`, Windows 10/11).
- **Code signing policy**: [`docs/code-signing-policy.md`](docs/code-signing-policy.md). Free
  code signing provided by SignPath.io, certificate by SignPath Foundation (pending approval;
  until then releases are unsigned pre-releases).
