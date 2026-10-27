/*
 * theme_internal.h - Shared between theme.c (document, window, tokens, fonts, strings, common
 * element fields) and theme_elements.c (the element type table). Not part of the public API.
 */
#ifndef LAUNCHER_CORE_THEME_INTERNAL_H
#define LAUNCHER_CORE_THEME_INTERNAL_H

#include "theme.h"

typedef struct {
    theme_model *theme;
    const json_doc *doc;
} theme_ctx;

/* Copies JSON string token tok into the pool. 0 if tok is not a string (silently) or the pool
 * is full (THEME_WARN_POOL_FULL). */
theme_sref theme_ctx_string(theme_ctx *c, int tok);

/* Reads an unsigned integer in [min, max]. Returns 1 and sets *out; returns 0 if tok is
 * missing (silently) or invalid (THEME_WARN_BAD_VALUE), leaving *out unchanged. */
int theme_ctx_uint(theme_ctx *c, int tok, uint32_t min, uint32_t max, uint32_t *out);

/* Blob index for a "<sha256>" string, or -1 for missing/null (silently) or invalid
 * (THEME_WARN_BAD_VALUE) or over THEME_MAX_BLOBS (THEME_WARN_LIMIT_BLOBS). */
int theme_ctx_blob(theme_ctx *c, int tok, int is_font);

/* Type-specific part of an element. The common fields (rect, font, color, text, align,
 * visible_when, style) are already filled in. Returns 1 to keep the element, 0 to skip it
 * (the parser then sets THEME_WARN_BAD_ELEMENT). */
typedef int (*theme_type_parse_fn)(theme_ctx *c, int object, theme_element *e);

typedef struct {
    const char *name;     /* "type" value in theme.json */
    uint8_t type;         /* theme_element_type */
    uint8_t align;        /* default theme_align */
    theme_type_parse_fn parse;
} theme_type_entry;

/* The element type table (theme_elements.c); lookup by name, 0 if unknown. */
const theme_type_entry *theme_type_find(const json_doc *doc, int name_tok);

#endif
