/*
 * manifest.c - See manifest.h.
 */
#include "manifest.h"
#include "path.h"
#include "str.h"

typedef struct {
    release_manifest *m;
    const json_doc *doc;
} parser;

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

int manifest_path_eq(const char *a, const char *b)
{
    while (*a && lower(*a) == lower(*b)) a++, b++;
    return lower(*a) == lower(*b);
}

int manifest_path_is_reserved(const char *path, const char *self_exe)
{
    static const char state_dir[] = MANIFEST_STATE_DIR;
    size_t i;
    if (manifest_path_eq(path, "launcher.json") || manifest_path_eq(path, "launcher.log")) return 1;
    if (self_exe && self_exe[0] && manifest_path_eq(path, self_exe)) return 1;
    for (i = 0; state_dir[i]; i++)
        if (lower(path[i]) != state_dir[i]) return 0;
    return path[i] == 0 || path[i] == '/';
}

const char *manifest_str(const release_manifest *m, uint32_t offset)
{
    return (const char *)m->pool + offset;
}

const uint8_t *manifest_bytes(const release_manifest *m, uint32_t offset)
{
    return m->pool + offset;
}

/* A raw (unescaped) JSON string token: its bytes in the text. */
static int raw_string(const json_doc *doc, int i, const char **s, size_t *n)
{
    if (json_kind(doc, i) != JSON_STRING || doc->tokens[i].escaped) return 0;
    *s = doc->text + doc->tokens[i].start;
    *n = doc->tokens[i].end - doc->tokens[i].start;
    return 1;
}

static manifest_error read_hash(const json_doc *doc, int i, uint8_t out[SHA256_BYTES])
{
    const char *s;
    size_t n;
    if (i < 0) return MANIFEST_ERR_OP;
    if (!raw_string(doc, i, &s, &n) || !hex_decode(s, n, out, SHA256_BYTES)) return MANIFEST_ERR_HASH;
    return MANIFEST_OK;
}

static manifest_error read_size(const json_doc *doc, int i, uint64_t *out)
{
    if (i < 0) return MANIFEST_ERR_OP;
    return json_u64(doc, i, out) ? MANIFEST_OK : MANIFEST_ERR_SIZE;
}

static manifest_error read_blob(const json_doc *doc, int object, const char *sha_key, const char *size_key,
                                manifest_blob *blob)
{
    manifest_error e = read_hash(doc, json_get(doc, object, sha_key), blob->sha256);
    return e ? e : read_size(doc, json_get(doc, object, size_key), &blob->size);
}

/* Decodes string token i into the pool; *offset gets its position, *len its length. */
static manifest_error pool_string(parser *p, int i, uint32_t *offset, uint32_t *len)
{
    release_manifest *m = p->m;
    size_t n;
    if (json_kind(p->doc, i) != JSON_STRING) return MANIFEST_ERR_OP;
    if (!json_string(p->doc, i, (char *)m->pool + m->pool_len, MANIFEST_POOL_BYTES - m->pool_len, &n))
        return MANIFEST_ERR_LIMIT;
    *offset = m->pool_len;
    if (len) *len = (uint32_t)n;
    m->pool_len += (uint32_t)n + 1;
    return MANIFEST_OK;
}

static manifest_error parse_file(parser *p, int op, manifest_op *out)
{
    release_manifest *m = p->m;
    const json_doc *doc = p->doc;
    manifest_error e = read_blob(doc, op, "sha256", "size", &out->target);
    int chunks = json_get(doc, op, "chunks"), c;
    uint64_t total = 0;
    if (e) return e;
    if (json_kind(doc, chunks) != JSON_ARRAY) return MANIFEST_ERR_OP;
    out->first = m->chunk_count;
    for (c = json_first(doc, chunks); c >= 0; c = json_next(doc, c)) {
        manifest_blob *chunk;
        if (m->chunk_count >= MANIFEST_CHUNKS_MAX) return MANIFEST_ERR_LIMIT;
        chunk = &m->chunks[m->chunk_count];
        if (json_kind(doc, c) != JSON_OBJECT) return MANIFEST_ERR_OP;
        e = read_blob(doc, c, "sha256", "size", chunk);
        if (e) return e;
        if (chunk->size == 0 || chunk->size > MANIFEST_CHUNK_MAX_BYTES) return MANIFEST_ERR_SIZE;
        total += chunk->size;
        if (total < chunk->size || total > out->target.size) return MANIFEST_ERR_SIZE;
        m->chunk_count++;
    }
    out->count = m->chunk_count - out->first;
    return total == out->target.size ? MANIFEST_OK : MANIFEST_ERR_SIZE;
}

static manifest_error parse_write(parser *p, int w, const manifest_op *op)
{
    release_manifest *m = p->m;
    manifest_write *out;
    const char *hex;
    size_t n;
    if (m->write_count >= MANIFEST_WRITES_MAX) return MANIFEST_ERR_LIMIT;
    out = &m->writes[m->write_count];
    if (json_kind(p->doc, w) != JSON_OBJECT || !json_u64(p->doc, json_get(p->doc, w, "offset"), &out->offset))
        return MANIFEST_ERR_WRITE;
    if (!raw_string(p->doc, json_get(p->doc, w, "data"), &hex, &n) || n == 0 || (n & 1))
        return MANIFEST_ERR_WRITE;
    n /= 2;
    if (n > MANIFEST_POOL_BYTES - m->pool_len) return MANIFEST_ERR_LIMIT;
    if (!hex_decode(hex, 2 * n, m->pool + m->pool_len, n)) return MANIFEST_ERR_WRITE;
    if (out->offset > op->base.size || n > op->base.size - out->offset) return MANIFEST_ERR_WRITE;
    out->data = m->pool_len;
    out->len = (uint32_t)n;
    m->pool_len += (uint32_t)n;
    m->write_count++;
    return MANIFEST_OK;
}

static manifest_error parse_patch(parser *p, int op, manifest_op *out)
{
    const json_doc *doc = p->doc;
    int writes = json_get(doc, op, "writes"), append = json_get(doc, op, "append"), w;
    manifest_error e = read_blob(doc, op, "base_sha256", "base_size", &out->base);
    if (!e) e = read_blob(doc, op, "result_sha256", "result_size", &out->target);
    if (e) return e;
    if (append >= 0 && !json_is_null(doc, append)) {
        if (json_kind(doc, append) != JSON_OBJECT) return MANIFEST_ERR_OP;
        e = read_blob(doc, append, "sha256", "size", &out->append);
        if (e) return e;
    }
    if (out->target.size < out->base.size || out->target.size - out->base.size != out->append.size)
        return MANIFEST_ERR_SIZE;
    if (writes >= 0 && json_kind(doc, writes) != JSON_ARRAY) return MANIFEST_ERR_OP;
    out->first = p->m->write_count;
    for (w = json_first(doc, writes); w >= 0; w = json_next(doc, w)) {
        e = parse_write(p, w, out);
        if (e) return e;
    }
    out->count = p->m->write_count - out->first;
    return MANIFEST_OK;
}

static int has_eol(const char *s)
{
    for (; *s; s++)
        if (*s == '\r' || *s == '\n') return 1;
    return 0;
}

static manifest_error parse_line(parser *p, int op, manifest_op *out)
{
    manifest_error e = pool_string(p, json_get(p->doc, op, "prefix"), &out->prefix, &out->prefix_len);
    if (!e) e = pool_string(p, json_get(p->doc, op, "line"), &out->line, &out->line_len);
    if (e) return e;
    if (has_eol(manifest_str(p->m, out->prefix)) || has_eol(manifest_str(p->m, out->line))) return MANIFEST_ERR_LINE;
    return MANIFEST_OK;
}

/* A file op must be the first op on its path; nothing may follow a patch on the same path. */
static manifest_error check_order(const release_manifest *m, uint32_t k)
{
    const manifest_op *op = &m->ops[k];
    const char *path = manifest_str(m, op->path);
    uint32_t j;
    for (j = 0; j < k; j++) {
        if (!manifest_path_eq(manifest_str(m, m->ops[j].path), path)) continue;
        if (op->kind == MANIFEST_OP_FILE || m->ops[j].kind == MANIFEST_OP_PATCH) return MANIFEST_ERR_ORDER;
    }
    return MANIFEST_OK;
}

static manifest_error parse_op(parser *p, int op, const char *self_exe)
{
    release_manifest *m = p->m;
    manifest_op *out;
    int kind = json_get(p->doc, op, "op");
    manifest_error e;
    if (m->op_count >= MANIFEST_OPS_MAX) return MANIFEST_ERR_LIMIT;
    out = &m->ops[m->op_count];
    *out = (manifest_op){0};
    if (json_kind(p->doc, op) != JSON_OBJECT) return MANIFEST_ERR_OP;
    if (json_string_eq(p->doc, kind, "file")) out->kind = MANIFEST_OP_FILE;
    else if (json_string_eq(p->doc, kind, "patch")) out->kind = MANIFEST_OP_PATCH;
    else if (json_string_eq(p->doc, kind, "line")) out->kind = MANIFEST_OP_LINE;
    else return MANIFEST_ERR_OP;
    e = pool_string(p, json_get(p->doc, op, "path"), &out->path, 0);
    if (e) return e == MANIFEST_ERR_LIMIT ? e : MANIFEST_ERR_PATH;
    if (!path_is_safe_relative(manifest_str(m, out->path)) || manifest_path_is_reserved(manifest_str(m, out->path), self_exe))
        return MANIFEST_ERR_PATH;
    switch (out->kind) {
    case MANIFEST_OP_FILE: e = parse_file(p, op, out); break;
    case MANIFEST_OP_PATCH: e = parse_patch(p, op, out); break;
    default: e = parse_line(p, op, out); break;
    }
    if (!e) e = check_order(m, m->op_count);
    if (!e) m->op_count++;
    return e;
}

manifest_error manifest_parse(release_manifest *m, const char *text, size_t len, json_token *tokens, uint32_t cap,
                              const char *self_exe, int *bad_op)
{
    json_doc doc;
    parser p = {m, &doc};
    uint64_t schema;
    int ops, op;
    manifest_error e;
    if (bad_op) *bad_op = -1;
    m->schema = 0;
    m->version[0] = 0;
    m->op_count = m->chunk_count = m->write_count = m->pool_len = 0;
    if (json_parse(&doc, text, len, tokens, cap) != JSON_OK || json_kind(&doc, 0) != JSON_OBJECT)
        return MANIFEST_ERR_JSON;
    if (!json_u64(&doc, json_get(&doc, 0, "schema"), &schema) || schema != MANIFEST_SCHEMA) return MANIFEST_ERR_SCHEMA;
    m->schema = (uint32_t)schema;
    if (!json_string(&doc, json_get(&doc, 0, "version"), m->version, sizeof m->version, 0) || !m->version[0])
        return MANIFEST_ERR_VERSION;
    ops = json_get(&doc, 0, "ops");
    if (json_kind(&doc, ops) != JSON_ARRAY) return MANIFEST_ERR_OP;
    for (op = json_first(&doc, ops); op >= 0; op = json_next(&doc, op)) {
        e = parse_op(&p, op, self_exe);
        if (e) {
            if (bad_op) *bad_op = (int)m->op_count;
            m->op_count = 0;
            return e;
        }
    }
    return MANIFEST_OK;
}

const char *manifest_error_text(manifest_error error)
{
    switch (error) {
    case MANIFEST_OK: return "ok";
    case MANIFEST_ERR_JSON: return "manifest is not a valid JSON object";
    case MANIFEST_ERR_SCHEMA: return "unsupported manifest schema";
    case MANIFEST_ERR_VERSION: return "invalid or missing \"version\"";
    case MANIFEST_ERR_OP: return "unknown op or missing field";
    case MANIFEST_ERR_PATH: return "unsafe or reserved path";
    case MANIFEST_ERR_HASH: return "invalid SHA-256";
    case MANIFEST_ERR_SIZE: return "sizes do not add up";
    case MANIFEST_ERR_WRITE: return "invalid patch write";
    case MANIFEST_ERR_LINE: return "line op with a line break";
    case MANIFEST_ERR_ORDER: return "ops on the same path in an order the journal cannot undo";
    case MANIFEST_ERR_LIMIT: return "manifest too large";
    }
    return "unknown error";
}
