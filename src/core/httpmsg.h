/*
 * httpmsg.h - The byte-level half of the HTTP/1.1 client, kept portable so it is unit tested:
 * building a GET request (optionally with Range) and parsing a response head.
 *
 * The launcher only speaks the subset of docs/spec.md: GET, one request per connection
 * (Connection: close), bodies framed by Content-Length (Transfer-Encoding is rejected).
 */
#ifndef LAUNCHER_CORE_HTTPMSG_H
#define LAUNCHER_CORE_HTTPMSG_H

#include <stddef.h>
#include <stdint.h>

#include "str.h"

#define HTTP_HEAD_MAX 8192

typedef struct {
    uint64_t first;
    uint64_t last;   /* inclusive; ignored when open_end */
    int open_end;    /* "bytes=first-" */
} http_range;

typedef struct {
    int status;                 /* 100-599 */
    int has_length;
    uint64_t content_length;
    int has_range;              /* Content-Range: bytes first-last/total */
    uint64_t range_first, range_last;
    int has_total;              /* total is "*" when 0 */
    uint64_t range_total;
} http_head;

typedef enum {
    HTTPMSG_OK = 0,
    HTTPMSG_ERR_STATUS_LINE,
    HTTPMSG_ERR_HEADER,
    HTTPMSG_ERR_LENGTH,
    HTTPMSG_ERR_RANGE,
    HTTPMSG_ERR_UNSUPPORTED     /* Transfer-Encoding */
} httpmsg_error;

/* A request target the launcher may send: starts with '/', printable ASCII, no spaces. */
int http_path_is_valid(const char *path);

/* Appends a complete GET request to sb; range may be 0. Returns sb->ok (0 also for a bad path). */
int http_build_get(strbuf *sb, const char *host, unsigned port, const char *path, const http_range *range);

/* Length of the head including the final CRLF CRLF, or 0 if buf does not hold all of it yet. */
size_t http_head_length(const char *buf, size_t len);

/* Parses a complete head (as delimited by http_head_length). For 206 it requires a
 * Content-Range consistent with Content-Length. */
httpmsg_error http_parse_head(const char *buf, size_t len, http_head *head);
const char *httpmsg_error_text(httpmsg_error error);

#endif
