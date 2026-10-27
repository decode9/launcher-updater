/*
 * manifest.h - The release manifest (docs/spec.md, "Release manifest"), schema 1, parsed into a
 * fixed-size model the install engine (install.h) works on.
 *
 * Strings and write data live in one byte pool and are referenced by offset (manifest_str /
 * manifest_bytes). The model is large (a few MB): give it static storage, never the stack.
 *
 * Validation rejects the whole manifest when:
 *  - schema is not 1, version is empty or too long, or an op has an unknown "op";
 *  - a path breaks path_is_safe_relative, or names the launcher itself: its executable
 *    (self_exe), launcher.json, launcher.log or anything under the state folder .launcher/
 *    (compared without ASCII case, as Windows does);
 *  - a hash is not 64 lowercase hex digits;
 *  - file: chunks do not add up to size, a chunk is empty or larger than 8 MiB, or a file of
 *    size 0 has chunks (a file with size 0 and no chunks is valid: an empty file);
 *  - patch: a write leaves the base, write data is not lowercase hex, or
 *    result_size != base_size + append.size ("append" may be absent: nothing appended);
 *  - line: prefix or line contains CR or LF;
 *  - order: a file op on a path that an earlier op already touched, more than one patch on a
 *    path, or any op on a path after its patch (the journal can only undo one patch per file).
 */
#ifndef LAUNCHER_CORE_MANIFEST_H
#define LAUNCHER_CORE_MANIFEST_H

#include <stddef.h>
#include <stdint.h>

#include "json.h"
#include "sha256.h"

#define MANIFEST_SCHEMA 1
#define MANIFEST_VERSION_BYTES 64
#define MANIFEST_OPS_MAX 4096
#define MANIFEST_CHUNKS_MAX 32768
#define MANIFEST_WRITES_MAX 16384
#define MANIFEST_POOL_BYTES (2u << 20)
#define MANIFEST_CHUNK_MAX_BYTES (8u << 20)

/* The launcher's own state folder (install.h), off limits for manifests. */
#define MANIFEST_STATE_DIR ".launcher"

typedef enum { MANIFEST_OP_FILE = 1, MANIFEST_OP_PATCH, MANIFEST_OP_LINE } manifest_op_kind;

typedef struct {
    uint8_t sha256[SHA256_BYTES];
    uint64_t size;
} manifest_blob;

typedef struct {
    uint64_t offset;
    uint32_t data; /* pool offset */
    uint32_t len;
} manifest_write;

typedef struct {
    uint8_t kind;          /* manifest_op_kind */
    uint32_t path;         /* pool offset, NUL-terminated */
    manifest_blob target;  /* file: the file; patch: the result */
    manifest_blob base;    /* patch */
    manifest_blob append;  /* patch (size 0: nothing to append) */
    uint32_t first, count; /* file: chunks[first..]; patch: writes[first..] */
    uint32_t prefix, line; /* line: pool offsets, NUL-terminated */
    uint32_t prefix_len, line_len;
} manifest_op;

typedef struct {
    uint32_t schema;
    char version[MANIFEST_VERSION_BYTES];
    uint32_t op_count, chunk_count, write_count, pool_len;
    manifest_op ops[MANIFEST_OPS_MAX];
    manifest_blob chunks[MANIFEST_CHUNKS_MAX];
    manifest_write writes[MANIFEST_WRITES_MAX];
    uint8_t pool[MANIFEST_POOL_BYTES];
} release_manifest;

typedef enum {
    MANIFEST_OK = 0,
    MANIFEST_ERR_JSON,
    MANIFEST_ERR_SCHEMA,
    MANIFEST_ERR_VERSION,
    MANIFEST_ERR_OP,       /* unknown op or missing/invalid field */
    MANIFEST_ERR_PATH,     /* unsafe path or one reserved for the launcher */
    MANIFEST_ERR_HASH,
    MANIFEST_ERR_SIZE,     /* sizes that do not add up, bad chunk size */
    MANIFEST_ERR_WRITE,    /* patch write outside the base or bad hex */
    MANIFEST_ERR_LINE,
    MANIFEST_ERR_ORDER,
    MANIFEST_ERR_LIMIT     /* more ops/chunks/writes/bytes than the model holds */
} manifest_error;

/* self_exe: file name of the running launcher (e.g. "launcher.exe"), compared without case.
 * On error, *bad_op (optional) receives the index of the offending op (or -1). */
manifest_error manifest_parse(release_manifest *m, const char *text, size_t len, json_token *tokens, uint32_t cap,
                              const char *self_exe, int *bad_op);
const char *manifest_error_text(manifest_error error);

const char *manifest_str(const release_manifest *m, uint32_t offset);
const uint8_t *manifest_bytes(const release_manifest *m, uint32_t offset);

/* ASCII case-insensitive equality of two paths (Windows file names). */
int manifest_path_eq(const char *a, const char *b);

/* 1 if a manifest may not touch path (launcher files and the state folder). */
int manifest_path_is_reserved(const char *path, const char *self_exe);

#endif
