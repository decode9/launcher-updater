/*
 * prefs.c - See prefs.h.
 */
#include "prefs.h"
#include "str.h"

static int is_letter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int prefs_lang_valid(const char *lang)
{
    size_t i, n;
    if (!lang || !is_letter(lang[0])) return 0;
    n = str_len(lang);
    if (n >= PREFS_LANG_BYTES || lang[n - 1] == '-') return 0;
    for (i = 0; i < n; i++)
        if (!is_letter(lang[i]) && !(lang[i] >= '0' && lang[i] <= '9') && lang[i] != '-') return 0;
    return 1;
}

int prefs_parse_lang(const char *text, size_t len, json_token *tokens, uint32_t cap, char *out)
{
    json_doc doc;
    out[0] = 0;
    if (json_parse(&doc, text, len, tokens, cap) != JSON_OK || json_kind(&doc, 0) != JSON_OBJECT) return 0;
    if (!json_string(&doc, json_get(&doc, 0, "lang"), out, PREFS_LANG_BYTES, 0) || !prefs_lang_valid(out)) {
        out[0] = 0;
        return 0;
    }
    return 1;
}

size_t prefs_format(char *out, size_t cap, const char *lang)
{
    strbuf sb;
    if (!lang || (lang[0] && !prefs_lang_valid(lang))) return 0;
    sb_init(&sb, out, cap);
    if (lang[0]) {
        sb_add(&sb, "{\"lang\": \"");
        sb_add(&sb, lang); /* valid tags need no escaping */
        sb_add(&sb, "\"}\n");
    } else {
        sb_add(&sb, "{}\n");
    }
    return sb.ok ? sb.len : 0;
}
