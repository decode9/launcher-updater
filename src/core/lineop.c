/*
 * lineop.c - See lineop.h.
 */
#include "lineop.h"

typedef struct {
    uint8_t *buf;
    size_t cap, len;
    int ok;
} out_buf;

static void put(out_buf *o, const void *data, size_t n)
{
    const uint8_t *p = data;
    if (!o->ok || n > o->cap - o->len) {
        o->ok = 0;
        return;
    }
    while (n--) o->buf[o->len++] = *p++;
}

static int is_blank(uint8_t c)
{
    return c == ' ' || c == '\t' || c == 0x0B || c == 0x0C;
}

static int body_starts_with(const uint8_t *body, size_t n, const char *prefix, size_t prefix_len)
{
    size_t i = 0, k;
    while (i < n && is_blank(body[i])) i++;
    if (n - i < prefix_len) return 0;
    for (k = 0; k < prefix_len; k++)
        if (body[i + k] != (uint8_t)prefix[k]) return 0;
    return 1;
}

int line_apply(const uint8_t *in, size_t len, const char *prefix, size_t prefix_len, const char *line,
               size_t line_len, uint8_t *out, size_t cap, size_t *out_len)
{
    out_buf o = {out, cap, 0, 1};
    size_t start = 0;
    while (start < len) {
        size_t end = start, next;
        while (end < len && in[end] != '\n' && in[end] != '\r') end++;
        next = end;
        if (next < len) next += in[next] == '\r' && next + 1 < len && in[next + 1] == '\n' ? 2 : 1;
        if (body_starts_with(in + start, end - start, prefix, prefix_len)) {
            put(&o, in, start);
            put(&o, line, line_len);
            put(&o, in + end, len - end); /* this line's ending and the rest, unchanged */
            *out_len = o.len;
            return o.ok;
        }
        start = next;
    }
    put(&o, in, len);
    if (len && in[len - 1] != '\n') put(&o, "\r\n", 2);
    put(&o, line, line_len);
    put(&o, "\r\n", 2);
    *out_len = o.len;
    return o.ok;
}
