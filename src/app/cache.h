/*
 * cache.h - The window's cache in .launcher/cache/ next to the executable: blobs named by their
 * SHA-256 (theme images and fonts, news images), the last theme document (theme.json) and the
 * last status document (status.json, used only when the server cannot be reached).
 *
 * Files are written to "<name>.part" and renamed, so a cached file is always complete; blobs
 * are verified against their SHA-256 again whenever they are loaded.
 */
#ifndef LAUNCHER_APP_CACHE_H
#define LAUNCHER_APP_CACHE_H

#include <stddef.h>
#include <stdint.h>

#include "core/sha256.h"

#define CACHE_DIR ".launcher/cache"

/* Creates the folder (needs app_dir). Returns 1 on success. */
int cache_init(void);

/* Copies the cached blob into buf if it is present and matches its SHA-256; returns its size,
 * or 0. Uses only the stack and buf, so the UI thread may call it (the window's blob loader). */
size_t cache_load_blob(const uint8_t sha[SHA256_BYTES], void *buf, size_t cap);

/* 1 if the blob is cached and intact. Worker or main thread (shares the download buffer). */
int cache_has_blob(const uint8_t sha[SHA256_BYTES]);

/* Makes sure the blob is cached, downloading it (verified) when it is missing or damaged.
 * size may be APP_SIZE_UNKNOWN. Worker thread only. Returns 1 if the blob is now cached. */
int cache_fetch_blob(const uint8_t sha[SHA256_BYTES], uint64_t size);

/* Writes a file at a native path through "<path>.part" + flush + atomic rename. */
int cache_write_atomic(const char *path, const void *data, size_t len);

/* Named documents ("theme.json", "status.json"). */
int cache_save(const char *name, const void *data, size_t len);
int cache_read(const char *name, void *buf, size_t cap, size_t *len);

#endif
