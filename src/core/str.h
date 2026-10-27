/*
 * str.h - Small string, hex and number helpers for the portable core.
 *
 * The core never calls libc: on Windows the launcher links without a C runtime, so everything
 * here is self-contained. No 64-bit division is used (it would need __aulldiv on i686).
 */
#ifndef LAUNCHER_CORE_STR_H
#define LAUNCHER_CORE_STR_H

#include <stddef.h>
#include <stdint.h>

size_t str_len(const char *s);
int str_eq(const char *a, const char *b);
int str_starts_with(const char *s, const char *prefix);
int mem_eq(const void *a, const void *b, size_t n);

/* Copies src (NUL-terminated) into dst; returns 0 (and writes an empty string) if it does not
 * fit in cap bytes including the terminator. */
int str_copy(char *dst, size_t cap, const char *src);

/* Copies exactly n bytes of src plus a terminator; returns 0 if n + 1 > cap. */
int str_copy_n(char *dst, size_t cap, const char *src, size_t n);

/* Lowercase hex. hex_decode accepts exactly 2 * n hex digits, lowercase only (the pin and blob
 * hash format of the spec); returns 0 on any other input. */
int hex_decode(const char *hex, size_t hex_len, uint8_t *out, size_t n);
void hex_encode(const uint8_t *data, size_t n, char *out); /* writes 2 * n chars + NUL */

/* Writes the decimal form of v; returns the number of chars (NUL appended). out >= 21 bytes. */
size_t u64_to_dec(uint64_t v, char *out);

/* Parses digits [s, s + n) as an unsigned decimal; returns 0 on empty input, non-digit or
 * overflow. */
int dec_to_u64(const char *s, size_t n, uint64_t *out);

/* Bounded string builder; once it overflows every later append is ignored and ok stays 0. */
typedef struct {
    char *buf;
    size_t cap, len;
    int ok;
} strbuf;

void sb_init(strbuf *sb, char *buf, size_t cap);
void sb_add(strbuf *sb, const char *s);
void sb_add_n(strbuf *sb, const char *s, size_t n);
void sb_add_char(strbuf *sb, char c);
void sb_add_u64(strbuf *sb, uint64_t v);
void sb_add_i64(strbuf *sb, int64_t v);
void sb_add_hex(strbuf *sb, const uint8_t *data, size_t n);

#endif
