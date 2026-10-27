/*
 * rt.c - The memory helpers clang emits calls to even with -ffreestanding (struct copies,
 * zero-initialised locals). The launcher links without a C runtime (/nodefaultlib).
 *
 * There is deliberately no __chkstk: a function with more than 4 KiB of locals would need it,
 * and the link then fails, which is how build.sh keeps every large buffer static.
 */
#include "rt.h"

void *memset(void *dst, int value, size_t n)
{
    unsigned char *d = dst;
    while (n--) *d++ = (unsigned char)value;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        while (n--) d[n] = s[n];
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++)
        if (*x != *y) return *x < *y ? -1 : 1;
    return 0;
}

/* MSVC-target objects that touch a float reference this marker, normally defined by the CRT. */
int _fltused = 0;
