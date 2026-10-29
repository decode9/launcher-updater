/*
 * prefs.h - The user's preferences document, .launcher/prefs.json next to the executable:
 *
 *   {"lang": "es"}
 *
 * Only the pure part lives here (parse, validate, format) so it can be tested on the host; the
 * app reads and writes the file (app/prefs.c). The document is tolerant on purpose: it is
 * written by the launcher itself, so a missing, corrupt or foreign file only means "no
 * preference" and never stops the launcher. Unknown fields are ignored.
 */
#ifndef LAUNCHER_CORE_PREFS_H
#define LAUNCHER_CORE_PREFS_H

#include <stddef.h>
#include <stdint.h>

#include "json.h"
#include "theme.h"

/* A language tag fits a theme language name (theme strings are looked up by it). */
#define PREFS_LANG_BYTES THEME_NAME_BYTES

/* Most a formatted document takes: {"lang": "<tag>"} plus a newline and the NUL. */
#define PREFS_DOC_BYTES (PREFS_LANG_BYTES + 16)

/* 1 if lang is a usable language tag: 1..PREFS_LANG_BYTES-1 characters of ASCII letters,
 * digits and '-', starting with a letter and not ending with '-' ("es", "pt-BR"). */
int prefs_lang_valid(const char *lang);

/* The "lang" of a prefs document into out (PREFS_LANG_BYTES). Returns 1 if the document is an
 * object with a valid "lang"; otherwise out is "" and it returns 0 (no preference). */
int prefs_parse_lang(const char *text, size_t len, json_token *tokens, uint32_t cap, char *out);

/* Writes the document for lang into out (at least PREFS_DOC_BYTES); "" writes {} (no
 * preference). Returns its length, or 0 if lang is neither "" nor valid. */
size_t prefs_format(char *out, size_t cap, const char *lang);

#endif
