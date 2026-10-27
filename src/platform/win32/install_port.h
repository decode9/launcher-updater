/*
 * install_port.h - The Win32 implementations of the install engine's ports (core/install.h):
 * files under the launcher folder (fs.h) and blobs over the HTTP client (http.h).
 *
 * Blobs come from the feed server (GET /v1/blob/<sha256>, pinned certificate) or, when
 * launcher.json has blobs_url, from <blobs_url>/<sha256> on that host without the pin: the
 * system validates its certificate chain and every blob is checked against the SHA-256 of the
 * pinned manifest (docs/spec.md, D-11). Single instance, not reentrant.
 */
#ifndef LAUNCHER_INSTALL_PORT_H
#define LAUNCHER_INSTALL_PORT_H

#include "core/config.h"
#include "core/install.h"

/* root: launcher folder (native). Returns 0 if blobs_url cannot be split. */
int install_port_init(const char *root, const launcher_config *cfg, install_fs *fs, install_net *net);

/* GET of a whole blob whose size is not known in advance (theme and news images), from the same
 * host as the engine's blobs. *size receives the length; the caller checks the SHA-256.
 * Returns 1 on a complete 200 response. */
/* Upper bound for a blob fetched without a known size (theme images, fonts, news): 8 MiB, as the UI buffer. */
#define INSTALL_PORT_WHOLE_MAX (8u * 1024u * 1024u)

int install_port_fetch_whole(const uint8_t sha256[SHA256_BYTES], install_sink sink, void *sink_user, uint64_t *size);

#endif
