/*
 * http.h - HTTP/1.1 GET over the pinned TLS client, with optional Range, streaming the body to
 * a callback. One request per connection (Connection: close), as docs/spec.md requires.
 *
 *     http_target t = {"play.example.com", 5103, pin};
 *     http_head head;
 *     http_error e = http_get(&t, "/v1/status", 0, &head, sink, user);
 *
 * Returns HTTP_OK once a complete response head was read. head->status tells what came back:
 *  - 200 / 206: the body (exactly Content-Length bytes, which is required) went to sink in
 *    order; a short body is HTTP_ERR_TRUNCATED. For 206 the Content-Range must start at the
 *    requested offset (and end at the requested last byte, unless open-ended).
 *  - anything else: the body is not read and sink is not called.
 * sink returns 0 to abort (HTTP_ERR_ABORTED). Not reentrant: one request at a time per
 * process (the connection and buffers are static).
 */
#ifndef LAUNCHER_HTTP_H
#define LAUNCHER_HTTP_H

#include <stddef.h>
#include <stdint.h>

#include "core/httpmsg.h"

typedef struct {
    const char *host;
    unsigned port;
    const uint8_t *pin; /* SHA256_BYTES */
    uint64_t max_body;  /* 0 = no limit; a larger Content-Length is refused before any byte is read */
} http_target;

typedef int (*http_sink)(void *user, const uint8_t *data, size_t size);

typedef enum {
    HTTP_OK = 0,
    HTTP_ERR_RESOLVE,
    HTTP_ERR_CONNECT,
    HTTP_ERR_TLS,
    HTTP_ERR_PIN,
    HTTP_ERR_REQUEST,     /* invalid path or request too large */
    HTTP_ERR_IO,
    HTTP_ERR_HEAD,        /* malformed or oversized response head */
    HTTP_ERR_NO_LENGTH,   /* 200/206 without Content-Length */
    HTTP_ERR_RANGE,       /* 206 for another range than asked */
    HTTP_ERR_TRUNCATED,
    HTTP_ERR_ABORTED,
    HTTP_ERR_TOO_LARGE    /* Content-Length above target->max_body */
} http_error;

http_error http_get(const http_target *target, const char *path, const http_range *range, http_head *head,
                    http_sink sink, void *user);
const char *http_error_text(http_error error);

#endif
