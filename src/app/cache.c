/*
 * cache.c - See cache.h.
 */
#include "cache.h"
#include "app_internal.h"
#include "core/str.h"
#include "log.h"
#include "platform/win32/fs.h"

static uint8_t g_buf[APP_BLOB_MAX];
static char g_path[FS_PATH_BYTES], g_part[FS_PATH_BYTES];

static int cache_file(char *out, size_t cap, const char *name)
{
    char rel[160];
    strbuf sb;
    sb_init(&sb, rel, sizeof rel);
    sb_add(&sb, CACHE_DIR "/");
    sb_add(&sb, name);
    return sb.ok && fs_join(out, cap, app_dir, rel);
}

static int blob_file(char *out, size_t cap, const uint8_t sha[SHA256_BYTES])
{
    char name[SHA256_BYTES * 2 + 1];
    hex_encode(sha, SHA256_BYTES, name);
    return cache_file(out, cap, name);
}

int cache_init(void)
{
    return cache_file(g_path, sizeof g_path, "x") && fs_make_parents(g_path);
}

size_t cache_load_blob(const uint8_t sha[SHA256_BYTES], void *buf, size_t cap)
{
    char file[FS_PATH_BYTES];
    uint8_t digest[SHA256_BYTES];
    size_t len = 0;
    if (!blob_file(file, sizeof file, sha) || !fs_read_all(file, buf, cap, &len)) return 0;
    sha256(buf, len, digest);
    return mem_eq(digest, sha, SHA256_BYTES) ? len : 0;
}

int cache_write_atomic(const char *target, const void *data, size_t len)
{
    fs_file f;
    int ok;
    strbuf sb;
    sb_init(&sb, g_part, sizeof g_part);
    sb_add(&sb, target);
    sb_add(&sb, ".part");
    if (!sb.ok || !fs_create(&f, g_part)) return 0;
    ok = fs_write(&f, data, len) && fs_flush(&f);
    fs_close(&f);
    if (ok) ok = fs_rename(g_part, target);
    if (!ok) fs_remove(g_part);
    return ok;
}

int cache_fetch_blob(const uint8_t sha[SHA256_BYTES], uint64_t size)
{
    strbuf *sb;
    size_t len = 0;
    if (size != APP_SIZE_UNKNOWN && size > sizeof g_buf) return 0;
    if (cache_load_blob(sha, g_buf, sizeof g_buf)) return 1;
    if (!app_fetch_blob(sha, size, g_buf, sizeof g_buf, &len) || !blob_file(g_path, sizeof g_path, sha) ||
        !cache_write_atomic(g_path, g_buf, len)) {
        sb = log_begin();
        sb_add(sb, "error: cannot cache blob ");
        sb_add_hex(sb, sha, SHA256_BYTES);
        log_end();
        return 0;
    }
    sb = log_begin();
    sb_add(sb, "cache: blob ");
    sb_add_hex(sb, sha, SHA256_BYTES);
    sb_add(sb, " size ");
    sb_add_u64(sb, len);
    log_end();
    return 1;
}

int cache_has_blob(const uint8_t sha[SHA256_BYTES])
{
    return cache_load_blob(sha, g_buf, sizeof g_buf) != 0;
}

int cache_save(const char *name, const void *data, size_t len)
{
    return cache_file(g_path, sizeof g_path, name) && cache_write_atomic(g_path, data, len);
}

int cache_read(const char *name, void *buf, size_t cap, size_t *len)
{
    *len = 0;
    return cache_file(g_path, sizeof g_path, name) && fs_read_all(g_path, buf, cap, len);
}
