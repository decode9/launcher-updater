/*
 * install_port.c - See install_port.h.
 */
#include "install_port.h"
#include "core/str.h"
#include "fs.h"
#include "http.h"

static char root_dir[FS_PATH_BYTES];
static char native_a[FS_PATH_BYTES], native_b[FS_PATH_BYTES];
static http_target target;
static char blob_host[CONFIG_HOST_BYTES];
static char blob_prefix[CONFIG_URL_BYTES];
static char request_path[CONFIG_URL_BYTES + 80];

static const char *native(char *out, const char *path)
{
    return fs_join(out, FS_PATH_BYTES, root_dir, path) ? out : 0;
}

static fs_file wrap(void *file)
{
    fs_file f;
    f.handle = file;
    return f;
}

static int p_open(void *user, const char *path, install_open_mode mode, void **file)
{
    fs_file f;
    int ok;
    (void)user;
    if (!native(native_a, path)) return 0;
    ok = mode == INSTALL_OPEN_READ ? fs_open_read(&f, native_a) : fs_open_rw(&f, native_a);
    *file = f.handle;
    return ok;
}

static int p_read(void *file, void *buf, size_t cap, size_t *got)
{
    fs_file f = wrap(file);
    return fs_read(&f, buf, cap, got);
}

static int p_write(void *file, const void *data, size_t size)
{
    fs_file f = wrap(file);
    return fs_write(&f, data, size);
}

static int p_seek(void *file, uint64_t offset)
{
    fs_file f = wrap(file);
    return fs_seek(&f, offset);
}

static int p_size(void *file, uint64_t *size)
{
    fs_file f = wrap(file);
    return fs_size(&f, size);
}

static int p_truncate(void *file, uint64_t size)
{
    fs_file f = wrap(file);
    return fs_truncate(&f, size);
}

static int p_flush(void *file)
{
    fs_file f = wrap(file);
    return fs_flush(&f);
}

static void p_close(void *file)
{
    fs_file f = wrap(file);
    fs_close(&f);
}

static int p_rename(void *user, const char *from, const char *to)
{
    (void)user;
    return native(native_a, from) && native(native_b, to) && fs_rename(native_a, native_b);
}

static int p_make_parents(void *user, const char *path)
{
    (void)user;
    return native(native_a, path) && fs_make_parents(native_a);
}

static int p_remove(void *user, const char *path)
{
    (void)user;
    return native(native_a, path) && fs_remove(native_a);
}

static int p_free_space(void *user, uint64_t *bytes)
{
    (void)user;
    return fs_free_space(root_dir, bytes);
}

static install_net_result p_fetch(void *user, const uint8_t sha256[SHA256_BYTES], uint64_t blob_size, uint64_t offset,
                                  uint64_t length, install_sink sink, void *sink_user)
{
    http_range range;
    http_head head;
    http_error e;
    strbuf sb;
    (void)user;
    sb_init(&sb, request_path, sizeof request_path);
    sb_add(&sb, blob_host[0] ? blob_prefix : "/v1/blob");
    sb_add_char(&sb, '/');
    sb_add_hex(&sb, sha256, SHA256_BYTES);
    if (!sb.ok || length == 0) return INSTALL_NET_FAILED;
    range.first = offset;
    range.last = offset + length - 1;
    range.open_end = 0;
    e = http_get(&target, request_path, offset == 0 && length == blob_size ? 0 : &range, &head, sink, sink_user);
    if (e == HTTP_ERR_ABORTED) return INSTALL_NET_ABORTED;
    if (e != HTTP_OK || (head.status != 200 && head.status != 206) || head.content_length != length)
        return INSTALL_NET_FAILED;
    return INSTALL_NET_OK;
}

int install_port_fetch_whole(const uint8_t sha256[SHA256_BYTES], install_sink sink, void *sink_user, uint64_t *size)
{
    http_head head;
    strbuf sb;
    sb_init(&sb, request_path, sizeof request_path);
    sb_add(&sb, blob_host[0] ? blob_prefix : "/v1/blob");
    sb_add_char(&sb, '/');
    sb_add_hex(&sb, sha256, SHA256_BYTES);
    if (!sb.ok || http_get(&target, request_path, 0, &head, sink, sink_user) != HTTP_OK || head.status != 200)
        return 0;
    *size = head.content_length;
    return 1;
}

int install_port_init(const char *root, const launcher_config *cfg, install_fs *fs, install_net *net)
{
    static const install_fs files = {0, p_open, p_read, p_write, p_seek, p_size, p_truncate, p_flush, p_close,
                                     p_rename, p_make_parents, p_remove, p_free_space};
    unsigned port = cfg->port;
    if (!str_copy(root_dir, sizeof root_dir, root)) return 0;
    blob_host[0] = 0;
    target.host = cfg->server;
    target.port = cfg->port;
    target.pin = cfg->fingerprint;
    if (cfg->has_blobs_url) {
        if (!config_split_https_url(cfg->blobs_url, blob_host, sizeof blob_host, &port, blob_prefix, sizeof blob_prefix))
            return 0;
        target.host = blob_host;
        target.port = port;
        target.pin = 0; /* D-11: system chain validation; integrity from the pinned manifest */
    }
    *fs = files;
    net->user = 0;
    net->fetch = p_fetch;
    return 1;
}
