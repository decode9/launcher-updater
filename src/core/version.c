/*
 * version.c - See version.h.
 */
#include "version.h"

static const char *parse_part(const char *s, uint32_t *out)
{
    uint64_t v = 0;
    const char *start = s;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (uint64_t)(*s - '0');
        if (v > 0xFFFFFFFFu) return 0;
        s++;
    }
    if (s == start || (s - start > 1 && *start == '0')) return 0;
    *out = (uint32_t)v;
    return s;
}

int version_parse(const char *text, version *out)
{
    version v;
    const char *s = parse_part(text, &v.major);
    if (!s || *s++ != '.') return 0;
    s = parse_part(s, &v.minor);
    if (!s || *s++ != '.') return 0;
    s = parse_part(s, &v.patch);
    if (!s || *s) return 0;
    *out = v;
    return 1;
}

static int cmp(uint32_t a, uint32_t b)
{
    return a < b ? -1 : a > b;
}

int version_compare(const version *a, const version *b)
{
    int r = cmp(a->major, b->major);
    if (!r) r = cmp(a->minor, b->minor);
    if (!r) r = cmp(a->patch, b->patch);
    return r;
}

int version_compare_str(const char *a, const char *b, int *result)
{
    version x, y;
    if (!version_parse(a, &x) || !version_parse(b, &y)) return 0;
    *result = version_compare(&x, &y);
    return 1;
}
