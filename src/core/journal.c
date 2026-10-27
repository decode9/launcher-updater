/*
 * journal.c - See journal.h.
 */
#include "journal.h"
#include "str.h"

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void put64(uint8_t *p, uint64_t v)
{
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t get64(const uint8_t *p)
{
    return (uint64_t)get32(p) | (uint64_t)get32(p + 4) << 32;
}

static void copy(uint8_t *dst, const void *src, size_t n)
{
    const uint8_t *s = src;
    while (n--) *dst++ = *s++;
}

static uint8_t *reserve(journal_builder *b, size_t n)
{
    uint8_t *p;
    if (!b->ok || n > b->cap - b->len) {
        b->ok = 0;
        return 0;
    }
    p = b->buf + b->len;
    b->len += n;
    return p;
}

void journal_begin(journal_builder *b, uint8_t *buf, size_t cap, uint64_t base_size, uint64_t result_size,
                   const uint8_t base_sha256[SHA256_BYTES], const uint8_t result_sha256[SHA256_BYTES],
                   const char *path)
{
    size_t path_len = str_len(path);
    uint8_t *p;
    b->buf = buf;
    b->cap = cap;
    b->len = 0;
    b->records = 0;
    b->ok = 1;
    p = reserve(b, JOURNAL_HEADER_BYTES + path_len + 4);
    if (!p) return;
    copy(p, "LJNL", 4);
    put32(p + 4, 1);
    put32(p + JOURNAL_STATUS_OFFSET, JOURNAL_PENDING);
    put32(p + 12, 0);
    put64(p + 16, base_size);
    put64(p + 24, result_size);
    copy(p + 32, base_sha256, SHA256_BYTES);
    copy(p + 64, result_sha256, SHA256_BYTES);
    put32(p + 96, (uint32_t)path_len);
    copy(p + JOURNAL_HEADER_BYTES, path, path_len);
    b->count_at = JOURNAL_HEADER_BYTES + path_len;
    put32(p + b->count_at, 0);
}

uint8_t *journal_add(journal_builder *b, uint64_t offset, uint32_t len)
{
    uint8_t *p = reserve(b, (size_t)JOURNAL_RECORD_BYTES + len);
    if (!p) return 0;
    put64(p, offset);
    put32(p + 8, len);
    b->records++;
    return p + JOURNAL_RECORD_BYTES;
}

size_t journal_finish(journal_builder *b)
{
    size_t body_end = b->len;
    uint8_t *p = reserve(b, 4 + SHA256_BYTES);
    if (!p) return 0;
    put32(b->buf + b->count_at, b->records);
    copy(p, "LEND", 4);
    sha256(b->buf + 12, body_end - 12, p + 4);
    return b->len;
}

int journal_parse(const uint8_t *buf, size_t len, journal_view *view)
{
    uint8_t digest[SHA256_BYTES];
    size_t body_end, pos;
    uint32_t i;
    if (len < JOURNAL_HEADER_BYTES + 4 + 4 + SHA256_BYTES) return 0;
    if (!mem_eq(buf, "LJNL", 4) || get32(buf + 4) != 1) return 0;
    body_end = len - 4 - SHA256_BYTES;
    if (!mem_eq(buf + body_end, "LEND", 4)) return 0;
    sha256(buf + 12, body_end - 12, digest);
    if (!mem_eq(digest, buf + body_end + 4, SHA256_BYTES)) return 0;
    view->status = get32(buf + JOURNAL_STATUS_OFFSET);
    view->base_size = get64(buf + 16);
    view->result_size = get64(buf + 24);
    copy(view->base_sha256, buf + 32, SHA256_BYTES);
    copy(view->result_sha256, buf + 64, SHA256_BYTES);
    view->path_len = get32(buf + 96);
    if (view->path_len > body_end - JOURNAL_HEADER_BYTES - 4) return 0;
    view->path = (const char *)buf + JOURNAL_HEADER_BYTES;
    pos = JOURNAL_HEADER_BYTES + view->path_len;
    view->records = get32(buf + pos);
    pos += 4;
    view->first_record = buf + pos;
    /* Every record must lie inside the body, and they must fill it exactly. */
    for (i = 0; i < view->records; i++) {
        uint32_t n;
        if (body_end - pos < JOURNAL_RECORD_BYTES) return 0;
        n = get32(buf + pos + 8);
        pos += JOURNAL_RECORD_BYTES;
        if (body_end - pos < n) return 0;
        pos += n;
    }
    return pos == body_end;
}

uint32_t journal_index(const journal_view *view, const uint8_t **records, uint32_t cap)
{
    const uint8_t *p = view->first_record;
    uint32_t i;
    if (view->records > cap) return 0;
    for (i = 0; i < view->records; i++) {
        records[i] = p;
        p += JOURNAL_RECORD_BYTES + get32(p + 8);
    }
    return view->records;
}

void journal_record(const uint8_t *record, uint64_t *offset, uint32_t *len, const uint8_t **old)
{
    *offset = get64(record);
    *len = get32(record + 8);
    *old = record + JOURNAL_RECORD_BYTES;
}
