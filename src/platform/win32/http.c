/*
 * http.c - See http.h. The byte-level parsing lives in core/httpmsg.c (unit tested).
 */
#include "http.h"
#include "tls.h"

#define BODY_CHUNK 0x4000

static tls_conn conn;
static char head_buf[HTTP_HEAD_MAX];
static char request_buf[1024];
static uint8_t body_buf[BODY_CHUNK];

static http_error from_tls(tls_error e)
{
    switch (e) {
    case TLS_OK: return HTTP_OK;
    case TLS_ERR_RESOLVE: return HTTP_ERR_RESOLVE;
    case TLS_ERR_CONNECT: return HTTP_ERR_CONNECT;
    case TLS_ERR_PIN: return HTTP_ERR_PIN;
    case TLS_ERR_IO: return HTTP_ERR_IO;
    case TLS_ERR_SIZE: return HTTP_ERR_REQUEST;
    case TLS_ERR_HANDSHAKE: break;
    }
    return HTTP_ERR_TLS;
}

/* Reads until the end of the head; *head_len gets its size and *have the bytes held so far. */
static http_error read_head(size_t *head_len, size_t *have)
{
    *have = 0;
    for (;;) {
        int n;
        if (*have >= sizeof head_buf) return HTTP_ERR_HEAD;
        n = tls_recv(&conn, (u8 *)head_buf + *have, (int)(sizeof head_buf - *have));
        if (n < 0) return HTTP_ERR_IO;
        if (n == 0) return HTTP_ERR_HEAD;
        *have += (size_t)n;
        *head_len = http_head_length(head_buf, *have);
        if (*head_len) return HTTP_OK;
    }
}

static int range_matches(const http_range *range, const http_head *head)
{
    if (!range) return head->status == 200;
    if (head->status == 200) return range->first == 0 && range->open_end; /* whole body is the answer */
    return head->range_first == range->first && (range->open_end || head->range_last == range->last);
}

static http_error stream_body(const http_head *head, size_t head_len, size_t have, http_sink sink, void *user)
{
    uint64_t left = head->content_length;
    size_t extra = have - head_len;
    if (extra > left) extra = (size_t)left; /* bytes after the body are ignored */
    if (extra) {
        if (!sink(user, (const uint8_t *)head_buf + head_len, extra)) return HTTP_ERR_ABORTED;
        left -= extra;
    }
    while (left) {
        int want = left < BODY_CHUNK ? (int)left : BODY_CHUNK;
        int n = tls_recv(&conn, body_buf, want);
        if (n < 0) return HTTP_ERR_IO;
        if (n == 0) return HTTP_ERR_TRUNCATED;
        if (!sink(user, body_buf, (size_t)n)) return HTTP_ERR_ABORTED;
        left -= (uint64_t)n;
    }
    return HTTP_OK;
}

static http_error exchange(const http_target *t, const char *path, const http_range *range, http_head *head,
                           http_sink sink, void *user)
{
    strbuf sb;
    size_t head_len = 0, have = 0;
    http_error e;
    head->status = 0;
    sb_init(&sb, request_buf, sizeof request_buf);
    if (!http_build_get(&sb, t->host, t->port, path, range)) return HTTP_ERR_REQUEST;
    e = from_tls(tls_open(&conn, t->host, t->port, t->pin));
    if (e) return e;
    e = from_tls(tls_send(&conn, (const u8 *)request_buf, (int)sb.len));
    if (e) return e;
    e = read_head(&head_len, &have);
    if (e) return e;
    if (http_parse_head(head_buf, head_len, head) != HTTPMSG_OK) return HTTP_ERR_HEAD;
    if (head->status != 200 && head->status != 206) return HTTP_OK;
    if (!head->has_length) return HTTP_ERR_NO_LENGTH;
    if (!range_matches(range, head)) return HTTP_ERR_RANGE;
    if (t->max_body && head->content_length > t->max_body) return HTTP_ERR_TOO_LARGE;
    return stream_body(head, head_len, have, sink, user);
}

http_error http_get(const http_target *target, const char *path, const http_range *range, http_head *head,
                    http_sink sink, void *user)
{
    http_error e = exchange(target, path, range, head, sink, user);
    tls_close(&conn);
    return e;
}

const char *http_error_text(http_error error)
{
    switch (error) {
    case HTTP_OK: return "ok";
    case HTTP_ERR_RESOLVE: return "cannot resolve host";
    case HTTP_ERR_CONNECT: return "cannot connect";
    case HTTP_ERR_TLS: return "TLS handshake failed";
    case HTTP_ERR_PIN: return "server certificate does not match the pinned fingerprint";
    case HTTP_ERR_REQUEST: return "invalid request";
    case HTTP_ERR_IO: return "connection failed";
    case HTTP_ERR_HEAD: return "malformed response head";
    case HTTP_ERR_NO_LENGTH: return "response without Content-Length";
    case HTTP_ERR_RANGE: return "response for another range";
    case HTTP_ERR_TOO_LARGE: return "response larger than expected";
    case HTTP_ERR_TRUNCATED: return "response body truncated";
    case HTTP_ERR_ABORTED: return "download aborted";
    }
    return "unknown error";
}
