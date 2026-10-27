/*
 * status.h - The server's GET /v1/status document (docs/spec.md, "/v1/status").
 *
 * Required: schema (= 1), server_time, open, theme, release. Optional: opens_at (null = no
 * date), news, launcher. Unknown fields are ignored. Times are UTC "YYYY-MM-DDTHH:MM:SSZ"
 * (an optional fraction of a second is accepted and dropped) and are kept as Unix seconds.
 * The countdown is opens_at - server_time: the PC clock is never used.
 */
#ifndef LAUNCHER_CORE_STATUS_H
#define LAUNCHER_CORE_STATUS_H

#include <stddef.h>
#include <stdint.h>

#include "json.h"
#include "sha256.h"

#define STATUS_SCHEMA 1
#define STATUS_NEWS_MAX 16
#define STATUS_TITLE_BYTES 256
#define STATUS_LINK_BYTES 512
#define STATUS_VERSION_BYTES 64

typedef struct {
    int present;
    char version[STATUS_VERSION_BYTES]; /* release: opaque; launcher: X.Y.Z; theme: empty */
    uint8_t sha256[SHA256_BYTES];
    uint64_t size;
} status_blob;

typedef struct {
    int has_image;
    uint8_t image[SHA256_BYTES];
    char title[STATUS_TITLE_BYTES]; /* in the requested language, else "en", else the first one */
    char link[STATUS_LINK_BYTES];   /* empty unless a valid https:// URL */
} status_news;

typedef struct {
    uint32_t schema;
    int64_t server_time;
    int open;
    int has_opens_at;
    int64_t opens_at;
    uint32_t news_count; /* extra items beyond STATUS_NEWS_MAX are ignored */
    status_news news[STATUS_NEWS_MAX];
    status_blob theme, release, launcher;
} launcher_status;

typedef enum {
    STATUS_OK = 0,
    STATUS_ERR_JSON,
    STATUS_ERR_SCHEMA,
    STATUS_ERR_TIME,
    STATUS_ERR_OPEN,
    STATUS_ERR_NEWS,
    STATUS_ERR_BLOB
} status_error;

/* lang: preferred language key for news titles (e.g. "es"); may be 0. Needs about
 * 32 + 16 * news tokens. */
status_error status_parse(launcher_status *st, const char *text, size_t len, json_token *tokens, uint32_t cap,
                          const char *lang);
const char *status_error_text(status_error error);

/* Seconds until opening (0 if already due), or -1 when there is no opens_at. */
int64_t status_seconds_to_open(const launcher_status *st);

/* "YYYY-MM-DDTHH:MM:SS[.fff]Z", years 1970-9999 -> Unix seconds. Returns 1 on success. */
int utc_parse(const char *text, size_t len, int64_t *out);

#endif
