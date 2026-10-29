/*
 * state.h - .launcher/state.json: what the install engine knows about the files it manages.
 *
 *   {"schema": 1, "release": "2030.01.01.1",
 *    "files": [{"path": "game.pak", "sha256": "<64 hex>", "size": 123, "owned": true}],
 *    "journals": ["game.pak"]}
 *
 * release: version of the last complete install; "" while an install is under way.
 * files: content each managed file had when the engine last wrote or verified it. A file whose
 *   entry matches and whose size on disk is the same is trusted without hashing it again.
 *   owned: the whole content came from a `file` op (the launcher created the file), so it may be
 *   deleted once no release mentions it. Absent (false) for files the launcher only edited
 *   (`patch`/`line` on a file it did not deliver, e.g. the user's settings) and in state files
 *   written before the field existed: those are only ever forgotten, never deleted.
 *   pending: sha256 all zero (size 0, owned): a `file` op whose download has not finished. The
 *   launcher may hold "<path>.part"; whatever is at path is not its content yet.
 * journals: paths that have a patch journal (.launcher/j-<id>.jnl, journal.h). Each one is undone
 *   (back to the patch base) before the next install.
 * Pure model and (de)serialisation; install.c does the file I/O. Large: use static storage.
 */
#ifndef LAUNCHER_CORE_STATE_H
#define LAUNCHER_CORE_STATE_H

#include <stddef.h>
#include <stdint.h>

#include "json.h"
#include "manifest.h"
#include "path.h"
#include "sha256.h"

#define STATE_SCHEMA 1
#define STATE_FILES_MAX MANIFEST_OPS_MAX
#define STATE_JOURNALS_MAX MANIFEST_PATCHES_MAX
/* Worst-case serialised size and token count. */
#define STATE_TEXT_MAX (STATE_FILES_MAX * (PATH_MAX_BYTES + 144) + STATE_JOURNALS_MAX * (PATH_MAX_BYTES + 4) + 256)
#define STATE_TOKENS_MAX (STATE_FILES_MAX * 9 + STATE_JOURNALS_MAX + 16)

typedef struct {
    char path[PATH_MAX_BYTES];
    uint8_t sha256[SHA256_BYTES];
    uint64_t size;
    int owned; /* 1: delivered whole by a `file` op (see above) */
} state_file;

typedef struct {
    char release[MANIFEST_VERSION_BYTES];
    uint32_t file_count, journal_count;
    state_file files[STATE_FILES_MAX];
    char journals[STATE_JOURNALS_MAX][PATH_MAX_BYTES];
} install_state;

void state_clear(install_state *s);

/* Returns 1 on success; on any error (bad JSON, schema, path, hash) s is left empty and 0 is
 * returned: the engine then simply re-verifies files by hashing them. */
int state_parse(install_state *s, const char *text, size_t len, json_token *tokens, uint32_t cap);

/* Serialises s into out; returns the length, or 0 if it does not fit. */
size_t state_write(const install_state *s, char *out, size_t cap);

const state_file *state_find(const install_state *s, const char *path);
int state_matches(const install_state *s, const char *path, const uint8_t sha256[SHA256_BYTES], uint64_t size);
/* Records path's content; 0 if full. state_set keeps the entry's owned flag (0 for a new entry):
 * edits (patch, line, journal undo) never change who created the file. state_set_owned sets it. */
int state_set(install_state *s, const char *path, const uint8_t sha256[SHA256_BYTES], uint64_t size);
int state_set_owned(install_state *s, const char *path, const uint8_t sha256[SHA256_BYTES], uint64_t size, int owned);
void state_forget(install_state *s, const char *path);
/* Marks path as a download under way (see pending above); 0 if full. */
int state_set_pending(install_state *s, const char *path);
int state_is_pending(const state_file *f);

int state_has_journal(const install_state *s, const char *path);
int state_add_journal(install_state *s, const char *path); /* 0 if full; no duplicates */
void state_remove_journal(install_state *s, const char *path);

#endif
