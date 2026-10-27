/*
 * httpmsg.c - See httpmsg.h.
 */
#include "httpmsg.h"

int http_path_is_valid(const char *path)
{
    if (path[0] != '/') return 0;
    for (; *path; path++)
        if ((unsigned char)*path <= 0x20 || (unsigned char)*path >= 0x7F) return 0;
    return 1;
}

int http_build_get(strbuf *sb, const char *host, unsigned port, const char *path, const http_range *range)
{
    if (!http_path_is_valid(path)) return 0;
    sb_add(sb, "GET ");
    sb_add(sb, path);
    sb_add(sb, " HTTP/1.1\r\nHost: ");
    sb_add(sb, host);
    if (port != 443) {
        sb_add_char(sb, ':');
        sb_add_u64(sb, port);
    }
    sb_add(sb, "\r\nUser-Agent: launcher\r\nAccept-Encoding: identity\r\nConnection: close\r\n");
    if (range) {
        sb_add(sb, "Range: bytes=");
        sb_add_u64(sb, range->first);
        sb_add_char(sb, '-');
        if (!range->open_end) sb_add_u64(sb, range->last);
        sb_add(sb, "\r\n");
    }
    sb_add(sb, "\r\n");
    return sb->ok;
}

size_t http_head_length(const char *buf, size_t len)
{
    size_t i;
    for (i = 3; i < len; i++)
        if (buf[i - 3] == '\r' && buf[i - 2] == '\n' && buf[i - 1] == '\r' && buf[i] == '\n') return i + 1;
    return 0;
}

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

static int name_is(const char *name, size_t n, const char *want)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (!want[i] || lower(name[i]) != want[i]) return 0;
    return want[n] == 0;
}

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

/* Parses digits at *p (up to end); advances *p. */
static int number(const char *s, size_t *p, size_t end, uint64_t *out)
{
    size_t start = *p;
    while (*p < end && is_digit(s[*p])) (*p)++;
    return dec_to_u64(s + start, *p - start, out);
}

/* "bytes FIRST-LAST/TOTAL", the same with an unknown total ("*"), or "bytes *" + "/TOTAL" (416). */
static int parse_content_range(const char *v, size_t n, http_head *h)
{
    size_t p = 6;
    if (n < 7 || !name_is(v, 6, "bytes ")) return 0;
    if (v[p] == '*') {
        p++;
    } else {
        if (!number(v, &p, n, &h->range_first) || p >= n || v[p++] != '-' || !number(v, &p, n, &h->range_last) ||
            h->range_last < h->range_first)
            return 0;
        h->has_range = 1;
    }
    if (p >= n || v[p++] != '/') return 0;
    if (p < n && v[p] == '*' && p + 1 == n) return h->has_range; /* "*" total only with a range */
    if (!number(v, &p, n, &h->range_total) || p != n) return 0;
    if (h->has_range && h->range_last >= h->range_total) return 0;
    h->has_total = 1;
    return 1;
}

static httpmsg_error parse_status_line(const char *s, size_t n, http_head *h)
{
    /* "HTTP/1.x NNN[ reason]" */
    if (n < 12 || !(name_is(s, 9, "http/1.1 ") || name_is(s, 9, "http/1.0 "))) return HTTPMSG_ERR_STATUS_LINE;
    if (!is_digit(s[9]) || !is_digit(s[10]) || !is_digit(s[11]) || (n > 12 && s[12] != ' '))
        return HTTPMSG_ERR_STATUS_LINE;
    h->status = (s[9] - '0') * 100 + (s[10] - '0') * 10 + (s[11] - '0');
    return h->status >= 100 && h->status <= 599 ? HTTPMSG_OK : HTTPMSG_ERR_STATUS_LINE;
}

static httpmsg_error parse_header(const char *s, size_t n, http_head *h)
{
    size_t colon = 0, v, e;
    uint64_t length;
    size_t p;
    while (colon < n && s[colon] != ':') colon++;
    if (colon == 0 || colon == n) return HTTPMSG_ERR_HEADER;
    for (v = 0; v < colon; v++)
        if (s[v] == ' ' || s[v] == '\t') return HTTPMSG_ERR_HEADER;
    v = colon + 1;
    e = n;
    while (v < e && (s[v] == ' ' || s[v] == '\t')) v++;
    while (e > v && (s[e - 1] == ' ' || s[e - 1] == '\t')) e--;
    if (name_is(s, colon, "content-length")) {
        p = v;
        if (!number(s, &p, e, &length) || p != e) return HTTPMSG_ERR_LENGTH;
        if (h->has_length && length != h->content_length) return HTTPMSG_ERR_LENGTH;
        h->has_length = 1;
        h->content_length = length;
    } else if (name_is(s, colon, "content-range")) {
        if (h->has_range || h->has_total || !parse_content_range(s + v, e - v, h)) return HTTPMSG_ERR_RANGE;
    } else if (name_is(s, colon, "transfer-encoding")) {
        return HTTPMSG_ERR_UNSUPPORTED;
    }
    return HTTPMSG_OK;
}

httpmsg_error http_parse_head(const char *buf, size_t len, http_head *h)
{
    size_t start = 0, i;
    int first = 1, ended = 0;
    httpmsg_error err;
    h->status = 0;
    h->has_length = h->has_range = h->has_total = 0;
    h->content_length = h->range_first = h->range_last = h->range_total = 0;
    for (i = 0; i + 1 < len; i++) {
        if (buf[i] != '\r' || buf[i + 1] != '\n') {
            if (buf[i] == '\r' || buf[i] == '\n' || buf[i] == 0) return first ? HTTPMSG_ERR_STATUS_LINE : HTTPMSG_ERR_HEADER;
            continue;
        }
        if (i == start) { /* empty line: end of head */
            ended = 1;
            break;
        }
        err = first ? parse_status_line(buf + start, i - start, h) : parse_header(buf + start, i - start, h);
        if (err) return err;
        first = 0;
        start = i + 2;
        i++;
    }
    if (first) return HTTPMSG_ERR_STATUS_LINE;
    if (!ended) return HTTPMSG_ERR_HEADER;
    if (h->status == 206) {
        if (!h->has_range || !h->has_length || h->content_length != h->range_last - h->range_first + 1)
            return HTTPMSG_ERR_RANGE;
    }
    return HTTPMSG_OK;
}

const char *httpmsg_error_text(httpmsg_error error)
{
    switch (error) {
    case HTTPMSG_OK: return "ok";
    case HTTPMSG_ERR_STATUS_LINE: return "bad status line";
    case HTTPMSG_ERR_HEADER: return "bad header";
    case HTTPMSG_ERR_LENGTH: return "bad Content-Length";
    case HTTPMSG_ERR_RANGE: return "bad Content-Range";
    case HTTPMSG_ERR_UNSUPPORTED: return "unsupported Transfer-Encoding";
    }
    return "unknown error";
}
