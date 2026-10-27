/*
 * json.c - See json.h. The parser is iterative (no recursion), so hostile nesting can only hit
 * JSON_MAX_DEPTH, never the stack.
 */
#include "json.h"
#include "str.h"

/* ---- parsing ------------------------------------------------------------- */

static int is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static size_t skip_ws(const json_doc *d, size_t p)
{
    while (p < d->len && is_ws(d->text[p])) p++;
    return p;
}

static int new_token(json_doc *d, int parent, json_type type, size_t start)
{
    json_token *t;
    if (d->count >= d->cap) return -1;
    t = &d->tokens[d->count];
    t->type = (uint8_t)type;
    t->escaped = 0;
    t->start = (uint32_t)start;
    t->end = (uint32_t)start;
    t->parent = parent;
    t->next = d->count + 1;
    t->count = 0;
    return (int)d->count++;
}

static int hex4(const char *s, uint32_t *out)
{
    uint32_t v = 0;
    int i;
    for (i = 0; i < 4; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
        else return 0;
    }
    *out = v;
    return 1;
}

/* Validates the string whose opening quote is at *p; leaves *p after the closing quote. */
static int scan_string(const json_doc *d, size_t *p, int *escaped)
{
    size_t i = *p + 1;
    *escaped = 0;
    while (i < d->len) {
        unsigned char c = (unsigned char)d->text[i];
        if (c == '"') {
            *p = i + 1;
            return 1;
        }
        if (c < 0x20) return 0;
        if (c != '\\') {
            i++;
            continue;
        }
        *escaped = 1;
        if (i + 1 >= d->len) return 0;
        c = (unsigned char)d->text[i + 1];
        if (c == 'u') {
            uint32_t cp, low;
            if (i + 6 > d->len || !hex4(d->text + i + 2, &cp)) return 0;
            i += 6;
            if (cp >= 0xDC00 && cp <= 0xDFFF) return 0; /* lone low surrogate */
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                if (i + 6 > d->len || d->text[i] != '\\' || d->text[i + 1] != 'u' ||
                    !hex4(d->text + i + 2, &low) || low < 0xDC00 || low > 0xDFFF)
                    return 0;
                i += 6;
            }
        } else if (c == '"' || c == '\\' || c == '/' || c == 'b' || c == 'f' || c == 'n' || c == 'r' ||
                   c == 't') {
            i += 2;
        } else {
            return 0;
        }
    }
    return 0;
}

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static int scan_number(const json_doc *d, size_t *p)
{
    size_t i = *p;
    if (i < d->len && d->text[i] == '-') i++;
    if (i >= d->len) return 0;
    if (d->text[i] == '0') {
        i++;
    } else if (is_digit(d->text[i])) {
        while (i < d->len && is_digit(d->text[i])) i++;
    } else {
        return 0;
    }
    if (i < d->len && d->text[i] == '.') {
        i++;
        if (i >= d->len || !is_digit(d->text[i])) return 0;
        while (i < d->len && is_digit(d->text[i])) i++;
    }
    if (i < d->len && (d->text[i] == 'e' || d->text[i] == 'E')) {
        i++;
        if (i < d->len && (d->text[i] == '+' || d->text[i] == '-')) i++;
        if (i >= d->len || !is_digit(d->text[i])) return 0;
        while (i < d->len && is_digit(d->text[i])) i++;
    }
    *p = i;
    return 1;
}

static int scan_literal(const json_doc *d, size_t *p, const char *word)
{
    size_t n = str_len(word);
    if (*p + n > d->len || !mem_eq(d->text + *p, word, n)) return 0;
    *p += n;
    return 1;
}

/* Parses `"key" :` at p for object `parent`; returns 0 on syntax error, -1 when out of tokens. */
static int parse_key(json_doc *d, size_t *p, int parent)
{
    int t, escaped;
    size_t start = *p;
    if (*p >= d->len || d->text[*p] != '"') return 0;
    t = new_token(d, parent, JSON_STRING, start + 1);
    if (t < 0) return -1;
    if (!scan_string(d, p, &escaped)) return 0;
    d->tokens[t].end = (uint32_t)(*p - 1);
    d->tokens[t].escaped = (uint8_t)escaped;
    d->tokens[parent].count++;
    *p = skip_ws(d, *p);
    if (*p >= d->len || d->text[*p] != ':') return 0;
    *p = skip_ws(d, *p + 1);
    return 1;
}

json_error json_parse(json_doc *d, const char *text, size_t len, json_token *tokens, uint32_t cap)
{
    size_t p;
    int parent = -1, depth = 0;
    d->text = text;
    d->len = len;
    d->tokens = tokens;
    d->count = 0;
    d->cap = cap;
    d->error_at = 0;
    p = skip_ws(d, 0);

value:
    {
        int t, escaped;
        size_t start = p;
        char c;
        if (p >= len) goto syntax;
        if (parent >= 0 && d->tokens[parent].type == JSON_ARRAY) d->tokens[parent].count++;
        c = text[p];
        if (c == '{' || c == '[') {
            t = new_token(d, parent, c == '{' ? JSON_OBJECT : JSON_ARRAY, start);
            if (t < 0) goto nomem;
            if (++depth > JSON_MAX_DEPTH) {
                d->error_at = p;
                return JSON_ERR_DEPTH;
            }
            parent = t;
            p = skip_ws(d, p + 1);
            if (p < len && text[p] == (c == '{' ? '}' : ']')) goto close;
            if (c == '{') {
                int r = parse_key(d, &p, parent);
                if (r < 0) goto nomem;
                if (r == 0) goto syntax;
            }
            goto value;
        }
        if (c == '"') {
            t = new_token(d, parent, JSON_STRING, start + 1);
            if (t < 0) goto nomem;
            if (!scan_string(d, &p, &escaped)) goto syntax;
            d->tokens[t].end = (uint32_t)(p - 1);
            d->tokens[t].escaped = (uint8_t)escaped;
            goto after;
        }
        if (c == '-' || is_digit(c)) {
            t = new_token(d, parent, JSON_NUMBER, start);
            if (t < 0) goto nomem;
            if (!scan_number(d, &p)) goto syntax;
        } else if (c == 't') {
            t = new_token(d, parent, JSON_TRUE, start);
            if (t < 0) goto nomem;
            if (!scan_literal(d, &p, "true")) goto syntax;
        } else if (c == 'f') {
            t = new_token(d, parent, JSON_FALSE, start);
            if (t < 0) goto nomem;
            if (!scan_literal(d, &p, "false")) goto syntax;
        } else if (c == 'n') {
            t = new_token(d, parent, JSON_NULL, start);
            if (t < 0) goto nomem;
            if (!scan_literal(d, &p, "null")) goto syntax;
        } else {
            goto syntax;
        }
        d->tokens[t].end = (uint32_t)p;
    }

after:
    p = skip_ws(d, p);
    if (parent < 0) {
        if (p != len) goto syntax;
        return JSON_OK;
    }
    if (p >= len) goto syntax;
    if (text[p] == ',') {
        p = skip_ws(d, p + 1);
        if (d->tokens[parent].type == JSON_OBJECT) {
            int r = parse_key(d, &p, parent);
            if (r < 0) goto nomem;
            if (r == 0) goto syntax;
        }
        goto value;
    }
    if (text[p] != (d->tokens[parent].type == JSON_OBJECT ? '}' : ']')) goto syntax;

close:
    d->tokens[parent].end = (uint32_t)(p + 1);
    d->tokens[parent].next = d->count;
    parent = d->tokens[parent].parent;
    depth--;
    p++;
    goto after;

syntax:
    d->error_at = p;
    return JSON_ERR_SYNTAX;
nomem:
    d->error_at = p;
    return JSON_ERR_NOMEM;
}

/* ---- navigation ---------------------------------------------------------- */

static const json_token *token(const json_doc *d, int i)
{
    return (i >= 0 && (uint32_t)i < d->count) ? &d->tokens[i] : 0;
}

int json_kind(const json_doc *d, int i)
{
    const json_token *t = token(d, i);
    return t ? t->type : -1;
}

uint32_t json_count(const json_doc *d, int i)
{
    const json_token *t = token(d, i);
    return t && (t->type == JSON_ARRAY || t->type == JSON_OBJECT) ? t->count : 0;
}

int json_first(const json_doc *d, int i)
{
    return json_count(d, i) ? i + 1 : -1;
}

int json_next(const json_doc *d, int i)
{
    const json_token *t = token(d, i);
    if (!t || t->next >= d->count || d->tokens[t->next].parent != t->parent || t->parent < 0) return -1;
    return (int)t->next;
}

int json_get(const json_doc *d, int object, const char *key)
{
    int k;
    if (json_kind(d, object) != JSON_OBJECT) return -1;
    for (k = json_first(d, object); k >= 0; k = json_next(d, json_next(d, k)))
        if (json_string_eq(d, k, key)) return json_next(d, k);
    return -1;
}

int json_at(const json_doc *d, int array, uint32_t n)
{
    int e;
    if (json_kind(d, array) != JSON_ARRAY || n >= json_count(d, array)) return -1;
    for (e = json_first(d, array); n; n--) e = json_next(d, e);
    return e;
}

/* ---- values -------------------------------------------------------------- */

int json_u64(const json_doc *d, int i, uint64_t *out)
{
    const json_token *t = token(d, i);
    if (!t || t->type != JSON_NUMBER) return 0;
    return dec_to_u64(d->text + t->start, t->end - t->start, out);
}

int json_bool(const json_doc *d, int i, int *out)
{
    int k = json_kind(d, i);
    if (k != JSON_TRUE && k != JSON_FALSE) return 0;
    *out = k == JSON_TRUE;
    return 1;
}

int json_is_null(const json_doc *d, int i)
{
    return json_kind(d, i) == JSON_NULL;
}

/* Decodes one source unit (raw byte or escape) at *pos into out; returns the byte count.
 * The string was validated by the parser, so this cannot fail. */
static int decode_unit(const char *s, size_t *pos, uint8_t out[4])
{
    size_t i = *pos;
    uint32_t cp, low;
    if (s[i] != '\\') {
        out[0] = (uint8_t)s[i];
        *pos = i + 1;
        return 1;
    }
    switch (s[i + 1]) {
    case 'b': out[0] = '\b'; break;
    case 'f': out[0] = '\f'; break;
    case 'n': out[0] = '\n'; break;
    case 'r': out[0] = '\r'; break;
    case 't': out[0] = '\t'; break;
    case 'u':
        hex4(s + i + 2, &cp);
        *pos = i + 6;
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            hex4(s + i + 8, &low);
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            *pos = i + 12;
        }
        if (cp < 0x80) {
            out[0] = (uint8_t)cp;
            return 1;
        }
        if (cp < 0x800) {
            out[0] = (uint8_t)(0xC0 | cp >> 6);
            out[1] = (uint8_t)(0x80 | (cp & 0x3F));
            return 2;
        }
        if (cp < 0x10000) {
            out[0] = (uint8_t)(0xE0 | cp >> 12);
            out[1] = (uint8_t)(0x80 | (cp >> 6 & 0x3F));
            out[2] = (uint8_t)(0x80 | (cp & 0x3F));
            return 3;
        }
        out[0] = (uint8_t)(0xF0 | cp >> 18);
        out[1] = (uint8_t)(0x80 | (cp >> 12 & 0x3F));
        out[2] = (uint8_t)(0x80 | (cp >> 6 & 0x3F));
        out[3] = (uint8_t)(0x80 | (cp & 0x3F));
        return 4;
    default: out[0] = (uint8_t)s[i + 1]; break; /* " \ / */
    }
    *pos = i + 2;
    return 1;
}

int json_string(const json_doc *d, int i, char *out, size_t cap, size_t *out_len)
{
    const json_token *t = token(d, i);
    size_t pos, n = 0;
    if (cap) out[0] = 0;
    if (!t || t->type != JSON_STRING || cap == 0) return 0;
    for (pos = t->start; pos < t->end;) {
        uint8_t unit[4];
        int k, m = decode_unit(d->text, &pos, unit);
        if (n + (size_t)m + 1 > cap) {
            out[0] = 0;
            return 0;
        }
        for (k = 0; k < m; k++) {
            if (unit[k] == 0) {
                out[0] = 0;
                return 0;
            }
            out[n++] = (char)unit[k];
        }
    }
    out[n] = 0;
    if (out_len) *out_len = n;
    return 1;
}

int json_string_eq(const json_doc *d, int i, const char *s)
{
    const json_token *t = token(d, i);
    size_t pos, n = 0;
    if (!t || t->type != JSON_STRING) return 0;
    if (!t->escaped) {
        size_t len = t->end - t->start;
        return str_len(s) == len && mem_eq(d->text + t->start, s, len);
    }
    for (pos = t->start; pos < t->end;) {
        uint8_t unit[4];
        int k, m = decode_unit(d->text, &pos, unit);
        for (k = 0; k < m; k++, n++)
            if (s[n] == 0 || (uint8_t)s[n] != unit[k]) return 0;
    }
    return s[n] == 0;
}
