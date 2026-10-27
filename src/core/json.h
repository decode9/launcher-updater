/*
 * json.h - Minimal strict JSON (RFC 8259) parser without dynamic allocation.
 *
 * json_parse tokenises the whole text into a token array the caller provides (the "arena"); the
 * text must stay alive while the document is used. Token 0 is the root value. Lookups take and
 * return token indexes; -1 means "missing" and every accessor accepts -1, so lookups chain:
 *
 *     int size = json_get(&doc, json_get(&doc, 0, "release"), "size");
 *     uint64_t n;
 *     if (!json_u64(&doc, size, &n)) ... missing or not an unsigned integer ...
 *
 * Strings stay escaped in the text; json_string decodes them (\uXXXX and surrogate pairs to
 * UTF-8) into a caller buffer. Object members are children in the order key, value, key, value.
 * Duplicate keys are allowed and json_get returns the first one.
 */
#ifndef LAUNCHER_CORE_JSON_H
#define LAUNCHER_CORE_JSON_H

#include <stddef.h>
#include <stdint.h>

#define JSON_MAX_DEPTH 64

typedef enum {
    JSON_NULL = 0,
    JSON_FALSE,
    JSON_TRUE,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} json_type;

typedef enum {
    JSON_OK = 0,
    JSON_ERR_SYNTAX,  /* malformed text (error_at says where) */
    JSON_ERR_NOMEM,   /* more tokens than the caller's array holds */
    JSON_ERR_DEPTH    /* nesting deeper than JSON_MAX_DEPTH */
} json_error;

typedef struct {
    uint8_t type;      /* json_type */
    uint8_t escaped;   /* string with backslash escapes */
    uint32_t start;    /* span in the text; for strings, inside the quotes */
    uint32_t end;
    int32_t parent;    /* -1 for the root */
    uint32_t next;     /* first token index after this token's subtree */
    uint32_t count;    /* array: elements; object: members */
} json_token;

typedef struct {
    const char *text;
    size_t len;
    json_token *tokens;
    uint32_t count, cap;
    size_t error_at;   /* byte offset of the first error */
} json_doc;

json_error json_parse(json_doc *doc, const char *text, size_t len, json_token *tokens, uint32_t cap);

/* Type of token i, or -1 if i is not a valid token. */
int json_kind(const json_doc *doc, int i);

int json_get(const json_doc *doc, int object, const char *key); /* value of key, or -1 */
int json_at(const json_doc *doc, int array, uint32_t n);        /* n-th element, or -1 */
uint32_t json_count(const json_doc *doc, int i);                /* 0 unless array/object */
int json_first(const json_doc *doc, int i);                     /* first child, or -1 */
int json_next(const json_doc *doc, int i);                      /* next sibling, or -1 */

/* Value readers: return 1 on success, 0 if the token is missing or of another kind. */
int json_u64(const json_doc *doc, int i, uint64_t *out); /* non-negative integer, no . or e */
int json_bool(const json_doc *doc, int i, int *out);
int json_is_null(const json_doc *doc, int i);

/* Decodes string i into out (NUL-terminated). Fails if it is not a string, does not fit in cap
 * bytes, or contains U+0000. *out_len (optional) receives the byte length. */
int json_string(const json_doc *doc, int i, char *out, size_t cap, size_t *out_len);

/* Compares the decoded string i with the NUL-terminated UTF-8 string s. */
int json_string_eq(const json_doc *doc, int i, const char *s);

#endif
