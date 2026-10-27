/*
 * journal.h - Undo journal of one patch op (docs/spec.md, "Journal"). Pure encode/decode; the
 * install engine writes it to .launcher/j-<id>.jnl and flushes it before touching the file.
 *
 * Layout (little-endian):
 *   0   "LJNL"            magic
 *   4   u32 1             format
 *   8   u32 status        0 pending (patch under way), 1 applied (the only field ever rewritten)
 *   12  u32 0             reserved
 *   16  u64 base_size     size before the append: undo truncates to it
 *   24  u64 result_size
 *   32  [32] base_sha256
 *   64  [32] result_sha256
 *   96  u32 path_len, path bytes
 *   ..  u32 record_count, then per in-place write: u64 offset, u32 len, len old bytes
 *   ..  "LEND", [32] SHA-256 of bytes [12, start of "LEND")
 * The trailer hash makes a torn or truncated journal detectable; status sits outside it so it
 * can be flipped in place. Undo = truncate to base_size, then restore the records in reverse
 * order. Undo is idempotent, so a crash during undo is harmless.
 */
#ifndef LAUNCHER_CORE_JOURNAL_H
#define LAUNCHER_CORE_JOURNAL_H

#include <stddef.h>
#include <stdint.h>

#include "sha256.h"

#define JOURNAL_PENDING 0
#define JOURNAL_APPLIED 1
#define JOURNAL_STATUS_OFFSET 8
#define JOURNAL_HEADER_BYTES 100
#define JOURNAL_RECORD_BYTES 12

typedef struct {
    uint8_t *buf;
    size_t cap, len;
    uint32_t records;
    size_t count_at; /* where record_count goes */
    int ok;
} journal_builder;

void journal_begin(journal_builder *b, uint8_t *buf, size_t cap, uint64_t base_size, uint64_t result_size,
                   const uint8_t base_sha256[SHA256_BYTES], const uint8_t result_sha256[SHA256_BYTES],
                   const char *path);
/* Reserves a record and returns where its len old bytes go (0 if the buffer is full). */
uint8_t *journal_add(journal_builder *b, uint64_t offset, uint32_t len);
/* Writes the trailer; returns the total length or 0 on overflow. */
size_t journal_finish(journal_builder *b);

typedef struct {
    uint32_t status;
    uint64_t base_size, result_size;
    uint8_t base_sha256[SHA256_BYTES], result_sha256[SHA256_BYTES];
    const char *path; /* not NUL-terminated */
    uint32_t path_len;
    uint32_t records;
    const uint8_t *first_record;
} journal_view;

/* Validates a whole journal (magic, format, bounds, trailer hash). Returns 1 if usable. */
int journal_parse(const uint8_t *buf, size_t len, journal_view *view);

/* Fills records[] with a pointer to each record, in order; returns the count, or 0 if there are
 * more than cap (a journal with no records also returns 0). */
uint32_t journal_index(const journal_view *view, const uint8_t **records, uint32_t cap);
void journal_record(const uint8_t *record, uint64_t *offset, uint32_t *len, const uint8_t **old);

#endif
