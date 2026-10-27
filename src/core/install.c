/*
 * install.c - See install.h.
 *
 * Crash safety rests on three rules:
 *  - a file only appears under its final name through an atomic rename of a verified file;
 *  - a patched file is only touched after its journal is on disk (flushed) and listed in
 *    state.json, and undoing a journal is idempotent;
 *  - state.json is replaced atomically (temporary file + flush + rename) and only ever claims
 *    content that was verified.
 */
#include "install.h"
#include "journal.h"
#include "lineop.h"
#include "state.h"
#include "str.h"

#define IO_BYTES (1u << 20)
#define SCRATCH_BYTES (5u << 20)
#define JOURNAL_MAX (MANIFEST_POOL_BYTES + MANIFEST_WRITES_MAX * JOURNAL_RECORD_BYTES + 1024)
#define REPORT_STEP (1u << 20)
#define AUX_BYTES (PATH_MAX_BYTES + 96)

#define STATE_TMP MANIFEST_STATE_DIR "/state.json.tmp"
#define LINE_TMP MANIFEST_STATE_DIR "/line.tmp"

/* Static state: the engine is single-threaded and not reentrant (install.h). */
static const install_ctx *cx;
static install_result *res;
static install_state state;
static char state_text[STATE_TEXT_MAX];
static json_token state_tokens[STATE_TOKENS_MAX];
static uint8_t io[IO_BYTES];
static uint8_t scratch[SCRATCH_BYTES]; /* journals; line op text in and out */
static const uint8_t *records[MANIFEST_WRITES_MAX];
static uint8_t present[MANIFEST_OPS_MAX];
static char aux[AUX_BYTES];
static uint64_t dl_done, dl_total;
static install_phase last_phase;
static uint64_t last_done;

_Static_assert(JOURNAL_MAX <= SCRATCH_BYTES, "a journal fits in scratch");
_Static_assert(2 * INSTALL_TEXT_MAX + MANIFEST_POOL_BYTES + 8 <= SCRATCH_BYTES, "line op text fits in scratch");

/* ---- helpers ---------------------------------------------------------------------------- */

static int cancelled(void)
{
    return cx->cancel && *cx->cancel;
}

static void copy_bytes(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--) *d++ = *s++;
}

static void report(install_phase phase, const char *path, uint64_t done, uint64_t total, int force)
{
    install_progress p;
    if (!cx->progress) return;
    if (!force && phase == last_phase && done >= last_done && done - last_done < REPORT_STEP && done != total) return;
    last_phase = phase;
    last_done = done;
    p.phase = phase;
    p.path = path ? path : "";
    p.done = done;
    p.total = total;
    cx->progress(cx->progress_user, &p);
}

static int open_file(const char *path, install_open_mode mode, void **file)
{
    return cx->fs->open(cx->fs->user, path, mode, file);
}

/* 1 and *size if the file exists (and can be opened). */
static int path_size(const char *path, uint64_t *size)
{
    void *f;
    int ok;
    if (!open_file(path, INSTALL_OPEN_READ, &f)) return 0;
    ok = cx->fs->size(f, size);
    cx->fs->close(f);
    return ok;
}

static int read_exact(void *f, uint8_t *buf, size_t n)
{
    size_t got;
    while (n) {
        if (!cx->fs->read(f, buf, n, &got) || got == 0) return 0;
        buf += got;
        n -= got;
    }
    return 1;
}

/* 1 read, 0 missing, -1 I/O error, -2 larger than cap. */
static int read_whole(const char *path, uint8_t *buf, size_t cap, size_t *len)
{
    void *f;
    uint64_t size;
    int r = 1;
    *len = 0;
    if (!open_file(path, INSTALL_OPEN_READ, &f)) return 0;
    if (!cx->fs->size(f, &size)) r = -1;
    else if (size > cap) r = -2;
    else if (!read_exact(f, buf, (size_t)size)) r = -1;
    else *len = (size_t)size;
    cx->fs->close(f);
    return r;
}

/* Replaces path's contents with data and flushes it (not atomic on its own). */
static int write_whole(const char *path, const void *data, size_t len)
{
    void *f;
    int ok;
    if (!open_file(path, INSTALL_OPEN_WRITE, &f)) return 0;
    ok = cx->fs->truncate(f, 0) && cx->fs->seek(f, 0) && cx->fs->write(f, data, len) && cx->fs->flush(f);
    cx->fs->close(f);
    return ok;
}

static int join_suffix(char *out, const char *a, const char *b)
{
    strbuf sb;
    sb_init(&sb, out, AUX_BYTES);
    sb_add(&sb, a);
    sb_add(&sb, b);
    return sb.ok;
}

/* .launcher/j-<16 hex of SHA-256(lowercase path)>.jnl */
static void journal_name(const char *path, char out[64])
{
    uint8_t digest[SHA256_BYTES];
    strbuf sb;
    size_t i;
    for (i = 0; path[i] && i + 1 < AUX_BYTES; i++) aux[i] = path[i] >= 'A' && path[i] <= 'Z' ? (char)(path[i] + 32) : path[i];
    aux[i] = 0;
    sha256(aux, i, digest);
    sb_init(&sb, out, 64);
    sb_add(&sb, MANIFEST_STATE_DIR "/j-");
    sb_add_hex(&sb, digest, 8);
    sb_add(&sb, ".jnl");
}

static void append_name(const uint8_t sha[SHA256_BYTES], char out[128])
{
    strbuf sb;
    sb_init(&sb, out, 128);
    sb_add(&sb, MANIFEST_STATE_DIR "/append-");
    sb_add_hex(&sb, sha, SHA256_BYTES);
    sb_add(&sb, ".part");
}

static install_error hash_file(const char *path, uint64_t *size_out, uint8_t out[SHA256_BYTES])
{
    void *f;
    uint64_t size, done = 0;
    sha256_ctx ctx;
    size_t got;
    if (!open_file(path, INSTALL_OPEN_READ, &f)) return INSTALL_ERR_IO;
    if (!cx->fs->size(f, &size)) {
        cx->fs->close(f);
        return INSTALL_ERR_IO;
    }
    sha256_init(&ctx);
    report(INSTALL_PHASE_VERIFY, path, 0, size, 1);
    for (;;) {
        if (cancelled()) {
            cx->fs->close(f);
            return INSTALL_ERR_CANCELLED;
        }
        if (!cx->fs->read(f, io, IO_BYTES, &got)) {
            cx->fs->close(f);
            return INSTALL_ERR_IO;
        }
        if (!got) break;
        sha256_update(&ctx, io, got);
        done += got;
        report(INSTALL_PHASE_VERIFY, path, done, size, 0);
    }
    cx->fs->close(f);
    sha256_final(&ctx, out);
    *size_out = done;
    return INSTALL_OK;
}

static int file_is(const char *path, const manifest_blob *blob, install_error *e)
{
    uint8_t digest[SHA256_BYTES];
    uint64_t size;
    *e = hash_file(path, &size, digest);
    return !*e && size == blob->size && mem_eq(digest, blob->sha256, SHA256_BYTES);
}

/* ---- state.json ------------------------------------------------------------------------- */

/* Loads state.json; 0 (and an empty state) when it is missing or unreadable. */
static int load_state(void)
{
    size_t len;
    if (read_whole(INSTALL_STATE_FILE, (uint8_t *)state_text, sizeof state_text, &len) != 1 ||
        !state_parse(&state, state_text, len, state_tokens, STATE_TOKENS_MAX)) {
        state_clear(&state);
        return 0;
    }
    return 1;
}

static install_error save_state(void)
{
    size_t n = state_write(&state, state_text, sizeof state_text);
    if (!n) return INSTALL_ERR_STATE;
    if (!write_whole(STATE_TMP, state_text, n) || !cx->fs->rename(cx->fs->user, STATE_TMP, INSTALL_STATE_FILE))
        return INSTALL_ERR_STATE;
    return INSTALL_OK;
}

static int is_current(const char *version)
{
    uint32_t i;
    uint64_t size;
    if (!state.release[0] || !str_eq(state.release, version)) return 0;
    for (i = 0; i < state.file_count; i++)
        if (!path_size(state.files[i].path, &size) || size != state.files[i].size) return 0;
    return 1;
}

/* ---- journals --------------------------------------------------------------------------- */

/* Restores a journal's records into an open file; 1 on success. */
static int restore(void *f, const journal_view *v)
{
    uint32_t n = journal_index(v, records, MANIFEST_WRITES_MAX), i;
    if (n != v->records) return 0;
    if (!cx->fs->truncate(f, v->base_size)) return 0;
    for (i = n; i-- > 0;) {
        uint64_t offset;
        uint32_t len;
        const uint8_t *old;
        journal_record(records[i], &offset, &len, &old);
        if (!cx->fs->seek(f, offset) || !cx->fs->write(f, old, len)) return 0;
    }
    return cx->fs->flush(f);
}

/* Puts path back to the base recorded in its journal, then drops the journal. A journal that
 * is missing or torn was never acted on (it is complete on disk before the file is touched),
 * so the file is only forgotten: the next steps re-verify it by hashing. */
static install_error undo_journal(const char *path)
{
    char name[64];
    journal_view v;
    size_t len;
    uint64_t size;
    void *f;
    int known = 0;
    install_error e;
    journal_name(path, name);
    if (read_whole(name, scratch, JOURNAL_MAX, &len) == 1 && journal_parse(scratch, len, &v) &&
        str_copy_n(aux, AUX_BYTES, v.path, v.path_len) && manifest_path_eq(aux, path) && path_size(path, &size) &&
        size >= v.base_size) {
        if (!open_file(path, INSTALL_OPEN_WRITE, &f)) return INSTALL_ERR_IO;
        known = restore(f, &v);
        cx->fs->close(f);
        if (!known) return INSTALL_ERR_IO; /* journal kept: retried next time */
    }
    if (known) {
        if (!state_set(&state, path, v.base_sha256, v.base_size)) return INSTALL_ERR_STATE;
    } else {
        state_forget(&state, path);
    }
    state_remove_journal(&state, path);
    e = save_state();
    if (e) return e;
    cx->fs->remove(cx->fs->user, name);
    return INSTALL_OK;
}

static install_error recover(void)
{
    uint32_t total = state.journal_count, done = 0;
    while (state.journal_count) {
        char path[PATH_MAX_BYTES];
        install_error e;
        str_copy(path, sizeof path, state.journals[state.journal_count - 1]);
        str_copy(res->path, sizeof res->path, path);
        report(INSTALL_PHASE_RECOVER, path, done, total, 1);
        e = undo_journal(path);
        if (e) return e;
        done++;
    }
    res->path[0] = 0;
    return INSTALL_OK;
}

/* ---- downloads -------------------------------------------------------------------------- */

typedef struct {
    void *file;
    sha256_ctx blob, whole; /* whole: the file op's running hash (unused for appends) */
    uint64_t got;
    size_t buffered;
    int failed;
    const char *path;
} file_sink;

static int sink_flush(file_sink *s)
{
    if (s->buffered && !cx->fs->write(s->file, io, s->buffered)) {
        s->failed = 1;
        return 0;
    }
    s->buffered = 0;
    return 1;
}

static int to_file(void *user, const uint8_t *data, size_t size)
{
    file_sink *s = user;
    if (cancelled() || s->failed) return 0;
    sha256_update(&s->blob, data, size);
    sha256_update(&s->whole, data, size);
    s->got += size;
    res->downloaded += size;
    dl_done += size;
    if (dl_done > dl_total) dl_total = dl_done;
    while (size) {
        size_t n = IO_BYTES - s->buffered;
        if (n > size) n = size;
        copy_bytes(io + s->buffered, data, n);
        s->buffered += n;
        data += n;
        size -= n;
        if (s->buffered == IO_BYTES && !sink_flush(s)) return 0;
    }
    report(INSTALL_PHASE_DOWNLOAD, s->path, dl_done, dl_total, 0);
    return 1;
}

static install_net_result fetch(const manifest_blob *blob, uint64_t offset, file_sink *s)
{
    return cx->net->fetch(cx->net->user, blob->sha256, blob->size, offset, blob->size - offset, to_file, s);
}

/* Downloads one chunk at offset of an open .part file, extending *whole on success. */
static install_error fetch_chunk(void *f, const manifest_blob *chunk, uint64_t offset, sha256_ctx *whole,
                                 const char *path)
{
    int attempt;
    for (attempt = 0; attempt < INSTALL_NET_ATTEMPTS; attempt++) {
        file_sink s = {0};
        uint8_t digest[SHA256_BYTES];
        install_net_result r;
        if (!cx->fs->seek(f, offset)) return INSTALL_ERR_IO;
        s.file = f;
        s.whole = *whole;
        s.path = path;
        sha256_init(&s.blob);
        r = fetch(chunk, 0, &s);
        if (r == INSTALL_NET_OK) sink_flush(&s);
        if (s.failed) return INSTALL_ERR_IO;
        if (cancelled()) return INSTALL_ERR_CANCELLED;
        if (r == INSTALL_NET_OK && s.got == chunk->size) {
            sha256_final(&s.blob, digest);
            if (mem_eq(digest, chunk->sha256, SHA256_BYTES)) {
                *whole = s.whole;
                return INSTALL_OK;
            }
            cx->fs->truncate(f, offset);
            return INSTALL_ERR_CORRUPT;
        }
        dl_done -= s.got; /* the partial chunk is thrown away */
        if (!cx->fs->truncate(f, offset)) return INSTALL_ERR_IO;
    }
    return INSTALL_ERR_NET;
}

/* Re-verifies a chunk already in the .part file; 1 if it is intact. */
static install_error verify_chunk(void *f, const manifest_blob *chunk, uint64_t offset, sha256_ctx *whole,
                                  const char *path, uint64_t total, int *ok)
{
    sha256_ctx c, w = *whole;
    uint8_t digest[SHA256_BYTES];
    uint64_t left = chunk->size;
    sha256_init(&c);
    if (!cx->fs->seek(f, offset)) return INSTALL_ERR_IO;
    while (left) {
        size_t n = left < IO_BYTES ? (size_t)left : IO_BYTES;
        if (cancelled()) return INSTALL_ERR_CANCELLED;
        if (!read_exact(f, io, n)) return INSTALL_ERR_IO;
        sha256_update(&c, io, n);
        sha256_update(&w, io, n);
        left -= n;
        report(INSTALL_PHASE_VERIFY, path, offset + chunk->size - left, total, 0);
    }
    sha256_final(&c, digest);
    *ok = mem_eq(digest, chunk->sha256, SHA256_BYTES);
    if (*ok) *whole = w;
    return INSTALL_OK;
}

static install_error assemble(void *f, const release_manifest *m, const manifest_op *op, const char *path,
                              sha256_ctx *whole)
{
    uint64_t have, offset = 0;
    uint32_t i;
    install_error e;
    if (!cx->fs->size(f, &have)) return INSTALL_ERR_IO;
    for (i = 0; i < op->count; i++) {
        const manifest_blob *chunk = &m->chunks[op->first + i];
        if (offset + chunk->size <= have) {
            int ok;
            e = verify_chunk(f, chunk, offset, whole, path, op->target.size, &ok);
            if (e) return e;
            if (ok) {
                offset += chunk->size;
                continue;
            }
        }
        if (have > offset) {
            if (!cx->fs->truncate(f, offset)) return INSTALL_ERR_IO;
            have = offset;
        }
        e = fetch_chunk(f, chunk, offset, whole, path);
        if (e) return e;
        offset += chunk->size;
        have = offset;
    }
    if (have > offset && !cx->fs->truncate(f, offset)) return INSTALL_ERR_IO;
    return cx->fs->flush(f) ? INSTALL_OK : INSTALL_ERR_IO;
}

static install_error download_file(const release_manifest *m, const manifest_op *op, const char *path)
{
    char part[AUX_BYTES];
    uint8_t digest[SHA256_BYTES];
    sha256_ctx whole;
    void *f;
    install_error e;
    if (!join_suffix(part, path, ".part")) return INSTALL_ERR_IO;
    if (!cx->fs->make_parents(cx->fs->user, path) || !open_file(part, INSTALL_OPEN_WRITE, &f)) return INSTALL_ERR_IO;
    sha256_init(&whole);
    e = assemble(f, m, op, path, &whole);
    cx->fs->close(f);
    if (e) return e;
    sha256_final(&whole, digest);
    if (!mem_eq(digest, op->target.sha256, SHA256_BYTES)) {
        cx->fs->remove(cx->fs->user, part);
        return INSTALL_ERR_CORRUPT;
    }
    if (!cx->fs->rename(cx->fs->user, part, path)) return INSTALL_ERR_IO;
    if (!state_set(&state, path, op->target.sha256, op->target.size)) return INSTALL_ERR_STATE;
    return save_state();
}

/* Downloads a patch's append blob into .launcher/append-<sha>.part (resumable). */
static install_error fetch_append(const manifest_blob *blob, const char *tmp, const char *path)
{
    int attempt;
    for (attempt = 0; attempt < INSTALL_NET_ATTEMPTS; attempt++) {
        file_sink s = {0};
        uint8_t digest[SHA256_BYTES];
        uint64_t have, left;
        install_net_result r = INSTALL_NET_OK;
        s.path = path;
        if (!open_file(tmp, INSTALL_OPEN_WRITE, &s.file)) return INSTALL_ERR_IO;
        sha256_init(&s.blob);
        sha256_init(&s.whole);
        if (!cx->fs->size(s.file, &have) || (have > blob->size && !cx->fs->truncate(s.file, 0))) goto io_error;
        if (have > blob->size) have = 0;
        /* Hash what an earlier run already fetched, then ask only for the rest. */
        left = have;
        if (!cx->fs->seek(s.file, 0)) goto io_error;
        while (left) {
            size_t n = left < IO_BYTES ? (size_t)left : IO_BYTES;
            if (!read_exact(s.file, io, n)) goto io_error;
            sha256_update(&s.blob, io, n);
            left -= n;
        }
        if (have < blob->size) {
            if (!cx->fs->seek(s.file, have)) goto io_error;
            r = fetch(blob, have, &s);
            sink_flush(&s); /* keep partial bytes: the next attempt resumes after them */
            if (s.failed || !cx->fs->flush(s.file)) goto io_error;
        }
        cx->fs->close(s.file);
        if (cancelled()) return INSTALL_ERR_CANCELLED;
        if (r != INSTALL_NET_OK || have + s.got != blob->size) continue;
        sha256_final(&s.blob, digest);
        if (mem_eq(digest, blob->sha256, SHA256_BYTES)) return INSTALL_OK;
        cx->fs->remove(cx->fs->user, tmp);
        return INSTALL_ERR_CORRUPT;
    io_error:
        cx->fs->close(s.file);
        return INSTALL_ERR_IO;
    }
    return INSTALL_ERR_NET;
}

/* ---- ops -------------------------------------------------------------------------------- */

/* Builds the journal of op in scratch from the file's current (base) bytes. */
static install_error build_journal(const release_manifest *m, const manifest_op *op, const char *path, size_t *len)
{
    journal_builder b;
    void *f;
    uint32_t i;
    int ok = 1;
    if (!open_file(path, INSTALL_OPEN_READ, &f)) return INSTALL_ERR_IO;
    journal_begin(&b, scratch, JOURNAL_MAX, op->base.size, op->target.size, op->base.sha256, op->target.sha256, path);
    for (i = 0; ok && i < op->count; i++) {
        const manifest_write *w = &m->writes[op->first + i];
        uint8_t *old = journal_add(&b, w->offset, w->len);
        ok = old && cx->fs->seek(f, w->offset) && read_exact(f, old, w->len);
    }
    cx->fs->close(f);
    *len = ok ? journal_finish(&b) : 0;
    return *len ? INSTALL_OK : INSTALL_ERR_IO;
}

static install_error write_patch(const release_manifest *m, const manifest_op *op, const char *path, const char *tmp)
{
    void *f, *t = 0;
    uint64_t left = op->append.size;
    uint32_t i;
    int ok;
    if (!open_file(path, INSTALL_OPEN_WRITE, &f)) return INSTALL_ERR_IO;
    ok = cx->fs->seek(f, op->base.size);
    if (ok && left) ok = open_file(tmp, INSTALL_OPEN_READ, &t);
    while (ok && left) {
        size_t n = left < IO_BYTES ? (size_t)left : IO_BYTES;
        ok = read_exact(t, io, n) && cx->fs->write(f, io, n);
        left -= n;
    }
    if (t) cx->fs->close(t);
    for (i = 0; ok && i < op->count; i++) {
        const manifest_write *w = &m->writes[op->first + i];
        ok = cx->fs->seek(f, w->offset) && cx->fs->write(f, manifest_bytes(m, w->data), w->len);
    }
    ok = ok && cx->fs->flush(f);
    cx->fs->close(f);
    return ok ? INSTALL_OK : INSTALL_ERR_IO;
}

static int mark_applied(const char *name)
{
    static const uint8_t applied[4] = {JOURNAL_APPLIED, 0, 0, 0};
    void *f;
    int ok;
    if (!open_file(name, INSTALL_OPEN_WRITE, &f)) return 0;
    ok = cx->fs->seek(f, JOURNAL_STATUS_OFFSET) && cx->fs->write(f, applied, 4) && cx->fs->flush(f);
    cx->fs->close(f);
    return ok;
}

static install_error apply_patch(const release_manifest *m, const manifest_op *op, const char *path)
{
    char tmp[128], name[64];
    uint64_t size;
    size_t len;
    install_error e = INSTALL_OK;
    /* 1. The file must be the base; nothing is touched otherwise. */
    if (!path_size(path, &size) || size != op->base.size) return INSTALL_ERR_BASE;
    if (!state_matches(&state, path, op->base.sha256, op->base.size)) {
        if (!file_is(path, &op->base, &e)) return e ? e : INSTALL_ERR_BASE;
        if (!state_set(&state, path, op->base.sha256, op->base.size)) return INSTALL_ERR_STATE;
    }
    /* 2. The tail, verified, before any change. */
    append_name(op->append.sha256, tmp);
    if (op->append.size) {
        e = fetch_append(&op->append, tmp, path);
        if (e) return e;
    }
    /* 3. Journal on disk and listed in the state before the first byte changes. */
    report(INSTALL_PHASE_PATCH, path, 0, 1, 1);
    e = build_journal(m, op, path, &len);
    if (e) return e;
    journal_name(path, name);
    if (!write_whole(name, scratch, len)) return INSTALL_ERR_IO;
    state_forget(&state, path);
    if (!state_add_journal(&state, path)) return INSTALL_ERR_STATE;
    e = save_state();
    if (e) return e;
    /* 4. Append, write, verify. */
    e = write_patch(m, op, path, tmp);
    if (e) return e;
    if (!file_is(path, &op->target, &e)) {
        if (e) return e; /* cancelled or I/O: the journal undoes it on the next run */
        e = undo_journal(path);
        state_forget(&state, path);
        if (!e) e = save_state();
        return e ? e : INSTALL_ERR_RESULT;
    }
    /* 5. Keep the journal (applied) for the return to the base on the next release. */
    if (!mark_applied(name)) return INSTALL_ERR_IO;
    if (!state_set(&state, path, op->target.sha256, op->target.size)) return INSTALL_ERR_STATE;
    e = save_state();
    if (!e) cx->fs->remove(cx->fs->user, tmp);
    report(INSTALL_PHASE_PATCH, path, 1, 1, 1);
    return e;
}

static install_error apply_line(const release_manifest *m, const manifest_op *op, const char *path)
{
    uint8_t *in = scratch, *out = scratch + INSTALL_TEXT_MAX, digest[SHA256_BYTES];
    size_t len, out_len;
    int r = read_whole(path, in, INSTALL_TEXT_MAX, &len);
    if (r == -2) return INSTALL_ERR_TEXT;
    if (r < 0) return INSTALL_ERR_IO;
    report(INSTALL_PHASE_LINE, path, 0, 1, 1);
    if (!line_apply(in, len, manifest_str(m, op->prefix), op->prefix_len, manifest_str(m, op->line), op->line_len,
                    out, SCRATCH_BYTES - INSTALL_TEXT_MAX, &out_len))
        return INSTALL_ERR_TEXT;
    if (r == 0 || out_len != len || !mem_eq(in, out, len)) {
        if (!cx->fs->make_parents(cx->fs->user, path) || !write_whole(LINE_TMP, out, out_len) ||
            !cx->fs->rename(cx->fs->user, LINE_TMP, path))
            return INSTALL_ERR_IO;
    }
    sha256(out, out_len, digest);
    if (!state_set(&state, path, digest, out_len)) return INSTALL_ERR_STATE;
    report(INSTALL_PHASE_LINE, path, 1, 1, 1);
    return save_state();
}

/* ---- plan ------------------------------------------------------------------------------- */

static uint64_t missing(uint64_t want, const char *partial)
{
    uint64_t have = 0;
    if (!path_size(partial, &have) || have > want) have = 0;
    return want - have;
}

static install_error plan(const release_manifest *m, uint64_t *need)
{
    char tmp[AUX_BYTES];
    uint32_t k;
    install_error e;
    *need = 0;
    for (k = 0; k < m->op_count; k++) {
        const manifest_op *op = &m->ops[k];
        const char *path = manifest_str(m, op->path);
        uint64_t size;
        present[k] = 0;
        if (op->kind == MANIFEST_OP_FILE) {
            if (path_size(path, &size) && size == op->target.size) {
                if (state_matches(&state, path, op->target.sha256, size)) {
                    present[k] = 1;
                } else if (file_is(path, &op->target, &e)) {
                    present[k] = 1;
                    if (!state_set(&state, path, op->target.sha256, size)) return INSTALL_ERR_STATE;
                } else if (e) {
                    return e;
                }
            }
            if (!present[k]) {
                /* Only whole chunks in .part are kept on resume. */
                uint64_t have = 0, kept = 0;
                uint32_t i;
                if (!join_suffix(tmp, path, ".part")) return INSTALL_ERR_IO;
                path_size(tmp, &have);
                for (i = 0; i < op->count && kept + m->chunks[op->first + i].size <= have; i++)
                    kept += m->chunks[op->first + i].size;
                *need += op->target.size - kept;
            }
        } else if (op->kind == MANIFEST_OP_PATCH && op->append.size) {
            append_name(op->append.sha256, tmp);
            *need += missing(op->append.size, tmp);
        }
    }
    return save_state();
}

static void prune(const release_manifest *m)
{
    uint32_t i = state.file_count, k;
    while (i-- > 0) {
        int used = 0;
        for (k = 0; k < m->op_count && !used; k++) used = manifest_path_eq(manifest_str(m, m->ops[k].path), state.files[i].path);
        if (!used) state_forget(&state, state.files[i].path);
    }
}

/* ---- entry points ----------------------------------------------------------------------- */

static install_error run(const release_manifest *m)
{
    uint64_t need, free_bytes;
    uint32_t k;
    install_error e;
    load_state();
    if (is_current(m->version)) {
        res->fast_path = 1;
        return INSTALL_OK;
    }
    if (!cx->fs->make_parents(cx->fs->user, INSTALL_STATE_FILE)) return INSTALL_ERR_STATE;
    state.release[0] = 0;
    e = save_state();
    if (!e) e = recover();
    if (!e) e = plan(m, &need);
    if (e) return e;
    if (cx->fs->free_space(cx->fs->user, &free_bytes) && free_bytes < need + INSTALL_SPACE_MARGIN) {
        res->need = need + INSTALL_SPACE_MARGIN;
        res->free = free_bytes;
        return INSTALL_ERR_SPACE;
    }
    dl_total = need;
    for (k = 0; k < m->op_count; k++) {
        const manifest_op *op = &m->ops[k];
        const char *path = manifest_str(m, op->path);
        res->op = (int)k;
        str_copy(res->path, sizeof res->path, path);
        if (cancelled()) return INSTALL_ERR_CANCELLED;
        switch (op->kind) {
        case MANIFEST_OP_FILE: e = present[k] ? INSTALL_OK : download_file(m, op, path); break;
        case MANIFEST_OP_PATCH: e = apply_patch(m, op, path); break;
        default: e = apply_line(m, op, path); break;
        }
        if (e) return e;
    }
    res->op = -1;
    res->path[0] = 0;
    prune(m);
    if (!str_copy(state.release, sizeof state.release, m->version)) return INSTALL_ERR_STATE;
    e = save_state();
    if (!e) cx->fs->remove(cx->fs->user, LINE_TMP);
    return e;
}

install_error install_run(const install_ctx *ctx, const release_manifest *m, install_result *result)
{
    cx = ctx;
    res = result;
    res->error = INSTALL_OK;
    res->op = -1;
    res->path[0] = 0;
    res->need = res->free = res->downloaded = 0;
    res->fast_path = 0;
    dl_done = dl_total = 0;
    last_phase = INSTALL_PHASE_DONE;
    last_done = 0;
    res->error = run(m);
    if (!res->error) report(INSTALL_PHASE_DONE, "", 1, 1, 1);
    return res->error;
}

int install_is_current(const install_ctx *ctx, const char *version)
{
    cx = ctx;
    load_state();
    return is_current(version);
}

int install_is_interrupted(const install_ctx *ctx)
{
    cx = ctx;
    return load_state() && !state.release[0];
}

const char *install_error_text(install_error error)
{
    switch (error) {
    case INSTALL_OK: return "ok";
    case INSTALL_ERR_CANCELLED: return "cancelled";
    case INSTALL_ERR_IO: return "file operation failed";
    case INSTALL_ERR_NET: return "download failed";
    case INSTALL_ERR_CORRUPT: return "downloaded data does not match its SHA-256";
    case INSTALL_ERR_SPACE: return "not enough free disk space";
    case INSTALL_ERR_BASE: return "file to patch is not the expected base";
    case INSTALL_ERR_RESULT: return "patched file does not match the expected result";
    case INSTALL_ERR_TEXT: return "text file too large for a line op";
    case INSTALL_ERR_STATE: return "cannot write the install state";
    }
    return "unknown error";
}
