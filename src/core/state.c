/*
 * state.c - See state.h.
 */
#include "state.h"
#include "str.h"

void state_clear(install_state *s)
{
    s->release[0] = 0;
    s->file_count = 0;
    s->journal_count = 0;
}

static int read_hash(const json_doc *doc, int i, uint8_t out[SHA256_BYTES])
{
    char hex[SHA256_HEX + 1];
    size_t n;
    return json_string(doc, i, hex, sizeof hex, &n) && hex_decode(hex, n, out, SHA256_BYTES);
}

static int parse_files(install_state *s, const json_doc *doc, int files)
{
    int f;
    if (files < 0) return 1;
    if (json_kind(doc, files) != JSON_ARRAY || json_count(doc, files) > STATE_FILES_MAX) return 0;
    for (f = json_first(doc, files); f >= 0; f = json_next(doc, f)) {
        state_file *e = &s->files[s->file_count];
        if (!json_string(doc, json_get(doc, f, "path"), e->path, sizeof e->path, 0) || !path_is_safe_relative(e->path) ||
            !read_hash(doc, json_get(doc, f, "sha256"), e->sha256) || !json_u64(doc, json_get(doc, f, "size"), &e->size))
            return 0;
        s->file_count++;
    }
    return 1;
}

static int parse_journals(install_state *s, const json_doc *doc, int journals)
{
    int j;
    if (journals < 0) return 1;
    if (json_kind(doc, journals) != JSON_ARRAY || json_count(doc, journals) > STATE_JOURNALS_MAX) return 0;
    for (j = json_first(doc, journals); j >= 0; j = json_next(doc, j)) {
        char *path = s->journals[s->journal_count];
        if (!json_string(doc, j, path, PATH_MAX_BYTES, 0) || !path_is_safe_relative(path)) return 0;
        s->journal_count++;
    }
    return 1;
}

int state_parse(install_state *s, const char *text, size_t len, json_token *tokens, uint32_t cap)
{
    json_doc doc;
    uint64_t schema;
    state_clear(s);
    if (json_parse(&doc, text, len, tokens, cap) != JSON_OK || json_kind(&doc, 0) != JSON_OBJECT ||
        !json_u64(&doc, json_get(&doc, 0, "schema"), &schema) || schema != STATE_SCHEMA ||
        !json_string(&doc, json_get(&doc, 0, "release"), s->release, sizeof s->release, 0) ||
        !parse_files(s, &doc, json_get(&doc, 0, "files")) || !parse_journals(s, &doc, json_get(&doc, 0, "journals"))) {
        state_clear(s);
        return 0;
    }
    return 1;
}

/* JSON string literal: quotes, backslashes and control characters escaped. */
static void add_json_string(strbuf *sb, const char *s)
{
    static const char digits[] = "0123456789abcdef";
    sb_add_char(sb, '"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            sb_add_char(sb, '\\');
            sb_add_char(sb, (char)c);
        } else if (c < 0x20) {
            sb_add(sb, "\\u00");
            sb_add_char(sb, digits[c >> 4]);
            sb_add_char(sb, digits[c & 15]);
        } else {
            sb_add_char(sb, (char)c);
        }
    }
    sb_add_char(sb, '"');
}

size_t state_write(const install_state *s, char *out, size_t cap)
{
    strbuf sb;
    uint32_t i;
    sb_init(&sb, out, cap);
    sb_add(&sb, "{\"schema\": 1, \"release\": ");
    add_json_string(&sb, s->release);
    sb_add(&sb, ",\n \"files\": [");
    for (i = 0; i < s->file_count; i++) {
        sb_add(&sb, i ? ",\n  {\"path\": " : "\n  {\"path\": ");
        add_json_string(&sb, s->files[i].path);
        sb_add(&sb, ", \"sha256\": \"");
        sb_add_hex(&sb, s->files[i].sha256, SHA256_BYTES);
        sb_add(&sb, "\", \"size\": ");
        sb_add_u64(&sb, s->files[i].size);
        sb_add_char(&sb, '}');
    }
    sb_add(&sb, "],\n \"journals\": [");
    for (i = 0; i < s->journal_count; i++) {
        if (i) sb_add(&sb, ", ");
        add_json_string(&sb, s->journals[i]);
    }
    sb_add(&sb, "]}\n");
    return sb.ok ? sb.len : 0;
}

const state_file *state_find(const install_state *s, const char *path)
{
    uint32_t i;
    for (i = 0; i < s->file_count; i++)
        if (manifest_path_eq(s->files[i].path, path)) return &s->files[i];
    return 0;
}

int state_matches(const install_state *s, const char *path, const uint8_t sha256[SHA256_BYTES], uint64_t size)
{
    const state_file *f = state_find(s, path);
    return f && f->size == size && mem_eq(f->sha256, sha256, SHA256_BYTES);
}

int state_set(install_state *s, const char *path, const uint8_t sha256[SHA256_BYTES], uint64_t size)
{
    state_file *f = (state_file *)state_find(s, path);
    if (!f) {
        if (s->file_count >= STATE_FILES_MAX) return 0;
        f = &s->files[s->file_count];
        if (!str_copy(f->path, sizeof f->path, path)) return 0;
        s->file_count++;
    }
    for (int i = 0; i < SHA256_BYTES; i++) f->sha256[i] = sha256[i];
    f->size = size;
    return 1;
}

void state_forget(install_state *s, const char *path)
{
    uint32_t i;
    for (i = 0; i < s->file_count; i++) {
        if (!manifest_path_eq(s->files[i].path, path)) continue;
        s->files[i] = s->files[--s->file_count];
        return;
    }
}

int state_has_journal(const install_state *s, const char *path)
{
    uint32_t i;
    for (i = 0; i < s->journal_count; i++)
        if (manifest_path_eq(s->journals[i], path)) return 1;
    return 0;
}

int state_add_journal(install_state *s, const char *path)
{
    if (state_has_journal(s, path)) return 1;
    if (s->journal_count >= STATE_JOURNALS_MAX) return 0;
    if (!str_copy(s->journals[s->journal_count], PATH_MAX_BYTES, path)) return 0;
    s->journal_count++;
    return 1;
}

void state_remove_journal(install_state *s, const char *path)
{
    uint32_t i;
    for (i = 0; i < s->journal_count; i++) {
        if (!manifest_path_eq(s->journals[i], path)) continue;
        s->journal_count--;
        if (i != s->journal_count) str_copy(s->journals[i], PATH_MAX_BYTES, s->journals[s->journal_count]);
        return;
    }
}
