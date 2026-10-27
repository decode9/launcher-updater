/*
 * lineop.h - The "line" op of the release manifest (docs/spec.md) on an in-memory text:
 *
 *  - lines end in "\r\n", "\n" or "\r" (the last line may have no ending);
 *  - the first line whose content, ignoring leading spaces, tabs, VT and FF, starts with prefix
 *    (exact bytes) is replaced by line, keeping that line's ending; later matches stay as they are;
 *  - with no match, line + "\r\n" is appended, preceded by "\r\n" when the text is not empty
 *    and does not end in "\n".
 */
#ifndef LAUNCHER_CORE_LINEOP_H
#define LAUNCHER_CORE_LINEOP_H

#include <stddef.h>
#include <stdint.h>

/* Writes the result to out (must not overlap in); returns 0 if it does not fit in cap. */
int line_apply(const uint8_t *in, size_t len, const char *prefix, size_t prefix_len, const char *line,
               size_t line_len, uint8_t *out, size_t cap, size_t *out_len);

#endif
