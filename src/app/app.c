/*
 * app.c - See app.h.
 */
#include "app.h"
#include "app_internal.h"
#include "core/manifest.h"
#include "log.h"
#include "platform/win32/http.h"
#include "platform/win32/install_port.h"
#include "platform/win32/rt.h"
#include "platform/win32/sys.h"

#define CONFIG_MAX 16384

/* Large state is static: the executable has no __chkstk, so stack frames stay under 4 KiB. */
char app_dir[FS_PATH_BYTES];
char app_exe_name[FS_PATH_BYTES];
launcher_config app_config;
launcher_status app_status;
static char path[FS_PATH_BYTES];
static char config_text[CONFIG_MAX];
static json_token tokens[1024];
static size_t status_len;

typedef struct {
    char *buf;
    size_t cap, len;
} memory_sink;

static char status_text[APP_STATUS_MAX];

static int to_memory(void *user, const uint8_t *data, size_t size)
{
    memory_sink *m = user;
    size_t i;
    if (size > m->cap - m->len) return 0;
    for (i = 0; i < size; i++) m->buf[m->len++] = (char)data[i];
    return 1;
}

int app_load_config(void)
{
    size_t len;
    config_error e;
    strbuf *sb;
    if (!fs_join(path, sizeof path, app_dir, APP_CONFIG_FILE) || !fs_read_all(path, config_text, sizeof config_text, &len)) {
        log_line("error: cannot read " APP_CONFIG_FILE " next to the launcher");
        return 0;
    }
    e = config_parse(&app_config, config_text, len, tokens, sizeof tokens / sizeof tokens[0]);
    if (e != CONFIG_OK) {
        sb = log_begin();
        sb_add(sb, "error: " APP_CONFIG_FILE ": ");
        sb_add(sb, config_error_text(e));
        log_end();
        return 0;
    }
    sb = log_begin();
    sb_add(sb, "server ");
    sb_add(sb, app_config.server);
    sb_add_char(sb, ':');
    sb_add_u64(sb, app_config.port);
    sb_add(sb, " pin ");
    sb_add_hex(sb, app_config.fingerprint, SHA256_BYTES);
    log_end();
    return 1;
}

static void log_blob(const char *name, const status_blob *blob)
{
    strbuf *sb = log_begin();
    sb_add(sb, name);
    if (!blob->present) {
        sb_add(sb, ": none");
    } else {
        sb_add(sb, ": ");
        if (blob->version[0]) {
            sb_add(sb, "version ");
            sb_add(sb, blob->version);
            sb_add_char(sb, ' ');
        }
        sb_add(sb, "sha256 ");
        sb_add_hex(sb, blob->sha256, SHA256_BYTES);
        sb_add(sb, " size ");
        sb_add_u64(sb, blob->size);
    }
    log_end();
}

static void log_status(void)
{
    strbuf *sb = log_begin();
    uint32_t i;
    sb_add(sb, "status: schema ");
    sb_add_u64(sb, app_status.schema);
    sb_add(sb, " open ");
    sb_add(sb, app_status.open ? "true" : "false");
    sb_add(sb, " server_time ");
    sb_add_i64(sb, app_status.server_time);
    if (app_status.has_opens_at) {
        sb_add(sb, " opens_at ");
        sb_add_i64(sb, app_status.opens_at);
        if (!app_status.open) {
            sb_add(sb, " opens_in ");
            sb_add_i64(sb, status_seconds_to_open(&app_status));
            sb_add(sb, "s");
        }
    } else {
        sb_add(sb, " opens_at null");
    }
    sb_add(sb, " news ");
    sb_add_u64(sb, app_status.news_count);
    log_end();
    for (i = 0; i < app_status.news_count; i++) {
        sb = log_begin();
        sb_add(sb, "news: ");
        sb_add(sb, app_status.news[i].title);
        if (app_status.news[i].link[0]) {
            sb_add(sb, " <");
            sb_add(sb, app_status.news[i].link);
            sb_add(sb, ">");
        }
        log_end();
    }
    log_blob("theme", &app_status.theme);
    log_blob("release", &app_status.release);
    log_blob("launcher", &app_status.launcher);
}

int app_fetch_status(void)
{
    http_target target = {app_config.server, app_config.port, app_config.fingerprint, 0};
    memory_sink sink = {status_text, sizeof status_text, 0};
    http_head head;
    http_error e = http_get(&target, "/v1/status", 0, &head, to_memory, &sink);
    status_error se;
    char lang[8];
    strbuf *sb;
    if (e != HTTP_OK) {
        sb = log_begin();
        sb_add(sb, "error: GET /v1/status: ");
        sb_add(sb, http_error_text(e));
        log_end();
        return 0;
    }
    if (head.status != 200) {
        sb = log_begin();
        sb_add(sb, "error: GET /v1/status: HTTP ");
        sb_add_u64(sb, (uint64_t)head.status);
        log_end();
        return 0;
    }
    status_len = sink.len;
    sys_language(lang, sizeof lang);
    se = status_parse(&app_status, status_text, sink.len, tokens, sizeof tokens / sizeof tokens[0], lang);
    if (se != STATUS_OK) {
        sb = log_begin();
        sb_add(sb, "error: /v1/status: ");
        sb_add(sb, status_error_text(se));
        log_end();
        return 0;
    }
    log_status();
    return 1;
}

const char *app_status_text(size_t *len)
{
    *len = status_len;
    return status_text;
}

int app_parse_status(const char *text, size_t len)
{
    char lang[8];
    sys_language(lang, sizeof lang);
    return status_parse(&app_status, text, len, tokens, sizeof tokens / sizeof tokens[0], lang) == STATUS_OK;
}

int app_begin(const char *mode)
{
    strbuf *sb;
    if (!fs_exe_dir(app_dir, sizeof app_dir)) return 0;
    if (fs_join(path, sizeof path, app_dir, APP_LOG_FILE)) log_open(path);
    sb = log_begin();
    sb_add(sb, "launcher " LAUNCHER_VERSION);
    if (mode && mode[0]) {
        sb_add_char(sb, ' ');
        sb_add(sb, mode);
    }
    log_end();
    return 1;
}

void app_end(void)
{
    log_close();
}

int app_run(int status_only)
{
    int code;
    if (!app_begin(status_only ? "--status" : "")) return 1;
    if (!app_load_config()) {
        code = 1;
    } else {
        code = app_fetch_status() ? 0 : 2;
    }
    app_end();
    return code;
}

/* ---- --install ---- */

#define MANIFEST_TEXT_MAX (4u << 20)
#define MANIFEST_TOKENS (MANIFEST_TEXT_MAX / 16)

static char manifest_text[MANIFEST_TEXT_MAX];
static json_token manifest_tokens[MANIFEST_TOKENS];
static release_manifest manifest;
static install_fs install_files;
static install_net install_blobs;
static install_result result;
static int ports_ready;
static install_phase logged_phase = INSTALL_PHASE_DONE;
static char logged_path[PATH_MAX_BYTES];
static uint64_t next_mark;

static const char *phase_name(install_phase phase)
{
    switch (phase) {
    case INSTALL_PHASE_RECOVER: return "recover";
    case INSTALL_PHASE_VERIFY: return "verify";
    case INSTALL_PHASE_DOWNLOAD: return "download";
    case INSTALL_PHASE_PATCH: return "patch";
    case INSTALL_PHASE_LINE: return "line";
    case INSTALL_PHASE_DONE: break;
    }
    return "done";
}

/* Logs each phase/file change and about every eighth of a download (no 64-bit division). */
void app_log_progress(const install_progress *p)
{
    strbuf *sb;
    if (p->phase == logged_phase && str_eq(p->path, logged_path) && p->done < next_mark) return;
    if (p->phase != logged_phase || !str_eq(p->path, logged_path)) next_mark = 0;
    logged_phase = p->phase;
    str_copy(logged_path, sizeof logged_path, p->path);
    next_mark = p->done + (p->total >> 3) + 1;
    sb = log_begin();
    sb_add(sb, "install: ");
    sb_add(sb, phase_name(p->phase));
    sb_add_char(sb, ' ');
    sb_add(sb, p->path);
    sb_add_char(sb, ' ');
    sb_add_u64(sb, p->done);
    sb_add_char(sb, '/');
    sb_add_u64(sb, p->total);
    log_end();
}

static int fetch_manifest(const install_ctx *ctx)
{
    memory_sink sink = {manifest_text, sizeof manifest_text, 0};
    uint8_t digest[SHA256_BYTES];
    manifest_error me;
    int bad;
    strbuf *sb;
    if (app_status.release.size > sizeof manifest_text ||
        ctx->net->fetch(ctx->net->user, app_status.release.sha256, app_status.release.size, 0, app_status.release.size, to_memory,
                        &sink) != INSTALL_NET_OK) {
        log_line("error: cannot download the release manifest");
        return 0;
    }
    sha256(manifest_text, sink.len, digest);
    if (sink.len != app_status.release.size || !mem_eq(digest, app_status.release.sha256, SHA256_BYTES)) {
        log_line("error: release manifest does not match its SHA-256");
        return 0;
    }
    me = manifest_parse(&manifest, manifest_text, sink.len, manifest_tokens, MANIFEST_TOKENS, app_exe_name, &bad);
    if (me != MANIFEST_OK) {
        sb = log_begin();
        sb_add(sb, "error: release manifest: ");
        sb_add(sb, manifest_error_text(me));
        sb_add(sb, " (op ");
        sb_add_i64(sb, bad);
        sb_add(sb, ")");
        log_end();
        return 0;
    }
    return 1;
}

int app_ports_init(void)
{
    if (ports_ready) return 1;
    if (!fs_exe_name(app_exe_name, sizeof app_exe_name) || !install_port_init(app_dir, &app_config, &install_files, &install_blobs)) {
        log_line("error: cannot prepare the install (folder or blobs_url)");
        return 0;
    }
    ports_ready = 1;
    return 1;
}

typedef struct {
    uint8_t *buf;
    size_t cap, len;
} blob_sink;

static int to_blob(void *user, const uint8_t *data, size_t size)
{
    blob_sink *b = user;
    if (size > b->cap - b->len) return 0;
    memcpy(b->buf + b->len, data, size);
    b->len += size;
    return 1;
}

int app_fetch_blob(const uint8_t sha[SHA256_BYTES], uint64_t size, void *buf, size_t cap, size_t *len)
{
    blob_sink sink = {buf, cap, 0};
    uint8_t digest[SHA256_BYTES];
    int attempt;
    uint64_t got = 0;
    if (!app_ports_init() || (size != APP_SIZE_UNKNOWN && size > cap)) return 0;
    for (attempt = 0; attempt < INSTALL_NET_ATTEMPTS; attempt++) {
        sink.len = 0;
        if (size == APP_SIZE_UNKNOWN ? install_port_fetch_whole(sha, to_blob, &sink, &got)
                                     : install_blobs.fetch(install_blobs.user, sha, size, 0, size, to_blob, &sink) ==
                                           INSTALL_NET_OK)
            break;
    }
    if (attempt == INSTALL_NET_ATTEMPTS || (size != APP_SIZE_UNKNOWN && sink.len != size)) return 0;
    if (len) *len = sink.len;
    sha256(buf, sink.len, digest);
    return mem_eq(digest, sha, SHA256_BYTES);
}

static void log_progress_cb(void *user, const install_progress *p)
{
    (void)user;
    app_log_progress(p);
}

int app_release_is_current(void)
{
    install_ctx ctx = {&install_files, &install_blobs, 0, 0, 0, app_exe_name};
    return app_status.release.present && app_ports_init() && install_is_current(&ctx, app_status.release.version);
}

int app_install_interrupted(void)
{
    install_ctx ctx = {&install_files, &install_blobs, 0, 0, 0, app_exe_name};
    return app_ports_init() && install_is_interrupted(&ctx);
}

int app_install_release(install_progress_fn progress, void *user, volatile const int *cancel,
                        install_result **out)
{
    install_ctx ctx = {&install_files, &install_blobs, progress ? progress : log_progress_cb, user, cancel, app_exe_name};
    strbuf *sb;
    if (out) *out = &result;
    result.error = INSTALL_OK;
    result.path[0] = 0;
    if (!app_status.release.present) {
        log_line("error: the server offers no release");
        return 2;
    }
    if (!app_ports_init()) return 1;
    if (install_is_current(&ctx, app_status.release.version)) {
        sb = log_begin();
        sb_add(sb, "install: release ");
        sb_add(sb, app_status.release.version);
        sb_add(sb, " already installed");
        log_end();
        return 0;
    }
    if (!fetch_manifest(&ctx)) {
        result.error = INSTALL_ERR_NET;
        return 2;
    }
    sb = log_begin();
    sb_add(sb, "install: release ");
    sb_add(sb, manifest.version);
    sb_add(sb, ", ");
    sb_add_u64(sb, manifest.op_count);
    sb_add(sb, " ops");
    log_end();
    install_run(&ctx, &manifest, &result);
    sb = log_begin();
    if (result.error == INSTALL_OK) {
        sb_add(sb, "install: done, downloaded ");
        sb_add_u64(sb, result.downloaded);
        sb_add(sb, " bytes");
    } else if (result.error == INSTALL_ERR_CANCELLED) {
        sb_add(sb, "install: cancelled by the user; the next run resumes it");
    } else {
        sb_add(sb, "error: install: ");
        sb_add(sb, install_error_text(result.error));
        if (result.path[0]) {
            sb_add(sb, " (");
            sb_add(sb, result.path);
            sb_add(sb, ")");
        }
        if (result.error == INSTALL_ERR_SPACE) {
            sb_add(sb, " need ");
            sb_add_u64(sb, result.need);
            sb_add(sb, " free ");
            sb_add_u64(sb, result.free);
        }
    }
    log_end();
    return result.error == INSTALL_OK ? 0 : (result.error == INSTALL_ERR_NET ? 2 : 3);
}

int app_install(void)
{
    int code;
    if (!app_begin("--install")) return 1;
    if (!app_load_config()) code = 1;
    else if (!app_fetch_status()) code = 2;
    else code = app_install_release(0, 0, 0, 0);
    app_end();
    return code;
}
