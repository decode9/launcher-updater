/*
 * str.c - See str.h.
 */
#include "str.h"

size_t str_len(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

int str_starts_with(const char *s, const char *prefix)
{
    while (*prefix)
        if (*s++ != *prefix++) return 0;
    return 1;
}

int mem_eq(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    while (n--)
        if (*x++ != *y++) return 0;
    return 1;
}

int str_copy_n(char *dst, size_t cap, const char *src, size_t n)
{
    size_t i;
    if (cap == 0) return 0;
    if (n + 1 > cap) {
        dst[0] = 0;
        return 0;
    }
    for (i = 0; i < n; i++) dst[i] = src[i];
    dst[n] = 0;
    return 1;
}

int str_copy(char *dst, size_t cap, const char *src)
{
    return str_copy_n(dst, cap, src, str_len(src));
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

int hex_decode(const char *hex, size_t hex_len, uint8_t *out, size_t n)
{
    size_t i;
    if (hex_len != 2 * n) return 0;
    for (i = 0; i < n; i++) {
        int hi = hex_value(hex[2 * i]), lo = hex_value(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return 0;
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    return 1;
}

void hex_encode(const uint8_t *data, size_t n, char *out)
{
    static const char digits[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; i++) {
        out[2 * i] = digits[data[i] >> 4];
        out[2 * i + 1] = digits[data[i] & 15];
    }
    out[2 * n] = 0;
}

/* v / 10 and v % 10 by shift-and-subtract: no runtime helper on 32-bit targets. */
static uint64_t divmod10(uint64_t v, unsigned *rem)
{
    uint64_t q = 0, r = 0;
    int bit;
    for (bit = 63; bit >= 0; bit--) {
        r = r << 1 | (v >> bit & 1);
        if (r >= 10) {
            r -= 10;
            q |= (uint64_t)1 << bit;
        }
    }
    *rem = (unsigned)r;
    return q;
}

size_t u64_to_dec(uint64_t v, char *out)
{
    char tmp[20];
    size_t n = 0, i;
    do {
        unsigned digit;
        v = divmod10(v, &digit);
        tmp[n++] = (char)('0' + digit);
    } while (v);
    for (i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    out[n] = 0;
    return n;
}

int dec_to_u64(const char *s, size_t n, uint64_t *out)
{
    const uint64_t limit = 1844674407370955161ULL; /* UINT64_MAX / 10 */
    uint64_t v = 0;
    size_t i;
    if (n == 0) return 0;
    for (i = 0; i < n; i++) {
        unsigned d;
        if (s[i] < '0' || s[i] > '9') return 0;
        d = (unsigned)(s[i] - '0');
        if (v > limit || (v == limit && d > 5)) return 0;
        v = v * 10 + d;
    }
    *out = v;
    return 1;
}

void sb_init(strbuf *sb, char *buf, size_t cap)
{
    sb->buf = buf;
    sb->cap = cap;
    sb->len = 0;
    sb->ok = cap > 0;
    if (cap) buf[0] = 0;
}

void sb_add_n(strbuf *sb, const char *s, size_t n)
{
    size_t i;
    if (!sb->ok) return;
    if (sb->len + n + 1 > sb->cap) {
        sb->ok = 0;
        return;
    }
    for (i = 0; i < n; i++) sb->buf[sb->len++] = s[i];
    sb->buf[sb->len] = 0;
}

void sb_add(strbuf *sb, const char *s)
{
    sb_add_n(sb, s, str_len(s));
}

void sb_add_char(strbuf *sb, char c)
{
    sb_add_n(sb, &c, 1);
}

void sb_add_u64(strbuf *sb, uint64_t v)
{
    char tmp[21];
    sb_add_n(sb, tmp, u64_to_dec(v, tmp));
}

void sb_add_i64(strbuf *sb, int64_t v)
{
    if (v < 0) {
        sb_add_char(sb, '-');
        sb_add_u64(sb, (uint64_t)0 - (uint64_t)v);
    } else {
        sb_add_u64(sb, (uint64_t)v);
    }
}

void sb_add_hex(strbuf *sb, const uint8_t *data, size_t n)
{
    static const char digits[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; i++) {
        sb_add_char(sb, digits[data[i] >> 4]);
        sb_add_char(sb, digits[data[i] & 15]);
    }
}
