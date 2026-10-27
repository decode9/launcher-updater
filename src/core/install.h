/*
 * install.h - Install/update engine: applies a release manifest (manifest.h) to the launcher
 * folder so that a power cut or a killed process never leaves a broken file behind.
 *
 * Portable: it only sees two injected ports. Paths given to the ports are spec paths (relative
 * to the launcher folder, '/' separated); the port maps them to native ones.
 *
 * What install_run does, in order:
 *   1. Fast path: state.json says this release is complete and every managed file still has
 *      its size on disk -> nothing to do.
 *   2. Marks the install as under way, then undoes every patch journal listed in the state:
 *      the half-done patch of an interrupted install and the applied patches of the previous
 *      release alike (truncate to the base size, restore old bytes in reverse order). Releases
 *      are always applied from the base.
 *   3. Plans: decides which `file` ops are already present (state entry with the same
 *      sha256 + size and the same size on disk, or else one full hash) and how many bytes are
 *      missing; fails with INSTALL_ERR_SPACE before downloading if the disk is too full.
 *   4. Applies the ops in manifest order:
 *      file  -> downloads into "<path>.part" chunk by chunk, verifying each chunk's SHA-256
 *               and the whole file's; on resume, complete chunks already in .part are
 *               re-verified and an incomplete one is truncated to its start; then an atomic
 *               rename over <path>. Size 0 with no chunks creates an empty file.
 *      patch -> checks the base, downloads `append` into .launcher/append-<sha>.part (resumable,
 *               verified), writes and flushes the journal, appends, applies the writes, hashes
 *               the result. A mismatch undoes the patch. The journal is kept (status applied)
 *               so the next release can return to the base.
 *      line  -> lineop.h on the whole text (at most INSTALL_TEXT_MAX bytes), written to a
 *               temporary file and renamed over the target.
 *   5. Records the release version in state.json and forgets files the manifest no longer
 *      mentions.
 * Everything the engine keeps lives in .launcher/ (state.json, journals, temporary files)
 * except the "<path>.part" downloads, which sit next to their target so the rename is atomic.
 *
 * Threading: install_run is synchronous and not reentrant (static buffers, ~10 MB). Run it on a
 * worker thread; it calls `progress` on that thread (post it to the UI thread) and polls
 * *cancel (set it from any thread) between buffers. Cancelling is a normal stop: whatever was
 * downloaded is kept and the next run resumes. The net port must not be used by anyone else
 * while install_run is running.
 */
#ifndef LAUNCHER_CORE_INSTALL_H
#define LAUNCHER_CORE_INSTALL_H

#include <stddef.h>
#include <stdint.h>

#include "manifest.h"
#include "path.h"
#include "sha256.h"

#define INSTALL_TEXT_MAX (1u << 20)        /* largest file a line op edits */
#define INSTALL_SPACE_MARGIN (16u << 20)   /* free bytes kept beyond the download */
#define INSTALL_NET_ATTEMPTS 3             /* per chunk / append blob */

#define INSTALL_STATE_FILE MANIFEST_STATE_DIR "/state.json"

/* ---- ports ------------------------------------------------------------------------------ */

typedef enum { INSTALL_OPEN_READ = 0, INSTALL_OPEN_WRITE } install_open_mode;

/* Files. Every function returns 1 on success, 0 on failure. */
typedef struct {
    void *user;
    /* READ: existing file. WRITE: read/write, created if missing, never truncated. Position 0. */
    int (*open)(void *user, const char *path, install_open_mode mode, void **file);
    int (*read)(void *file, void *buf, size_t cap, size_t *got); /* got = 0 at end of file */
    int (*write)(void *file, const void *data, size_t size);     /* all of it */
    int (*seek)(void *file, uint64_t offset);
    int (*size)(void *file, uint64_t *size);
    int (*truncate)(void *file, uint64_t size); /* only shrinks; the position is then unspecified */
    int (*flush)(void *file);                   /* to the disk, not only to the OS */
    void (*close)(void *file);
    int (*rename)(void *user, const char *from, const char *to); /* replaces `to` atomically */
    int (*make_parents)(void *user, const char *path);          /* mkdir -p of path's folder */
    int (*remove)(void *user, const char *path);                /* 1 also if it did not exist */
    int (*free_space)(void *user, uint64_t *bytes);             /* 0 if unknown (check skipped) */
} install_fs;

typedef int (*install_sink)(void *user, const uint8_t *data, size_t size); /* 0 aborts */

typedef enum { INSTALL_NET_OK = 0, INSTALL_NET_FAILED, INSTALL_NET_ABORTED } install_net_result;

/* Blobs. fetch streams bytes [offset, offset + length) of the blob sha256 (blob_size bytes in
 * total) to sink, in order. ABORTED when sink returned 0. The engine counts the bytes and
 * verifies hashes itself; the port only transports. */
typedef struct {
    void *user;
    install_net_result (*fetch)(void *user, const uint8_t sha256[SHA256_BYTES], uint64_t blob_size, uint64_t offset,
                                uint64_t length, install_sink sink, void *sink_user);
} install_net;

/* ---- progress --------------------------------------------------------------------------- */

typedef enum {
    INSTALL_PHASE_RECOVER = 0, /* undoing patch journals; done/total = journals */
    INSTALL_PHASE_VERIFY,      /* hashing one file; done/total = bytes of that file */
    INSTALL_PHASE_DOWNLOAD,    /* done/total = bytes of the whole download plan */
    INSTALL_PHASE_PATCH,       /* applying a patch; done/total = ops */
    INSTALL_PHASE_LINE,        /* done/total = ops */
    INSTALL_PHASE_DONE
} install_phase;

typedef struct {
    install_phase phase;
    const char *path; /* file being worked on ("" if none); valid during the call only */
    uint64_t done, total;
} install_progress;

typedef void (*install_progress_fn)(void *user, const install_progress *progress);

typedef struct {
    const install_fs *fs;
    const install_net *net;
    install_progress_fn progress; /* optional; throttled to about one call per MiB */
    void *progress_user;
    volatile const int *cancel;   /* optional; non-zero stops the install */
    const char *self_exe;         /* file name of the launcher (never touched) */
} install_ctx;

/* ---- result ----------------------------------------------------------------------------- */

typedef enum {
    INSTALL_OK = 0,
    INSTALL_ERR_CANCELLED,
    INSTALL_ERR_IO,       /* a file operation failed */
    INSTALL_ERR_NET,      /* the blob could not be downloaded (after retries) */
    INSTALL_ERR_CORRUPT,  /* downloaded bytes do not match their SHA-256 */
    INSTALL_ERR_SPACE,    /* not enough free disk space (need/free in the result) */
    INSTALL_ERR_BASE,     /* patch: the file is not the expected base; nothing was touched */
    INSTALL_ERR_RESULT,   /* patch: the result does not match; the patch was undone */
    INSTALL_ERR_TEXT,     /* line: the file is larger than INSTALL_TEXT_MAX */
    INSTALL_ERR_STATE     /* state.json cannot be written (or is full) */
} install_error;

typedef struct {
    install_error error;
    int op;                     /* failing op index, -1 if none */
    char path[PATH_MAX_BYTES];  /* file involved, "" if none */
    uint64_t need, free;        /* INSTALL_ERR_SPACE */
    uint64_t downloaded;        /* bytes fetched during this run */
    int fast_path;              /* 1 if nothing had to be done */
} install_result;

install_error install_run(const install_ctx *ctx, const release_manifest *m, install_result *result);

/* 1 if state.json says `version` is installed and complete and every managed file still has
 * its recorded size (the fast path, without downloading the manifest). */
int install_is_current(const install_ctx *ctx, const char *version);

/* 1 if state.json records an install that started and did not finish (cancelled, killed or
 * failed): the next install_run resumes it. */
int install_is_interrupted(const install_ctx *ctx);

const char *install_error_text(install_error error);

#endif
