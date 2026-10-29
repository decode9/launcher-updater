/*
 * test_install.c - Install engine over a POSIX file port (in a temporary folder) and an
 * in-memory blob store, with fault injection: "dying" at the n-th mutating file operation or
 * network fetch makes every later operation fail, as if the process had been killed there.
 */
#define _POSIX_C_SOURCE 200809L
#include "testlib.h"
#include "../src/core/install.h"
#include "../src/core/lineop.h"
#include "../src/core/state.h"
#include "../src/core/str.h"

#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- fault-injecting POSIX port ---- */

static char root[256];
static int dead, ops_left = -1; /* ops_left: mutating ops until death (-1 never) */
/* A file that exists but cannot be opened (locked by another program); locked_probe_fails
 * makes even the existence check fail. writes counts every operation that may change the disk. */
static const char *locked;
static int locked_probe_fails, writes;
static uint64_t fake_free = (uint64_t)1 << 40;

static int alive_op(void)
{
    writes++;
    if (dead) return 0;
    if (ops_left == 0) {
        dead = 1;
        return 0;
    }
    if (ops_left > 0) ops_left--;
    return 1;
}

static void native(char *out, const char *path)
{
    snprintf(out, 600, "%s/%s", root, path);
}

static int p_open(void *u, const char *path, install_open_mode mode, void **file)
{
    char n[600];
    int fd;
    (void)u;
    if (dead || (locked && strcmp(path, locked) == 0)) return 0;
    if (mode == INSTALL_OPEN_WRITE) writes++;
    native(n, path);
    fd = open(n, mode == INSTALL_OPEN_READ ? O_RDONLY : O_RDWR | O_CREAT, 0644);
    if (fd < 0) return 0;
    *file = (void *)(intptr_t)(fd + 1);
    return 1;
}
#define FD(f) ((int)(intptr_t)(f) - 1)
static int p_read(void *f, void *buf, size_t cap, size_t *got)
{
    ssize_t n;
    if (dead) return 0;
    n = read(FD(f), buf, cap > 4096 ? 4096 : cap); /* short reads on purpose */
    if (n < 0) return 0;
    *got = (size_t)n;
    return 1;
}
static int p_write(void *f, const void *data, size_t size)
{
    if (!alive_op()) return 0;
    return write(FD(f), data, size) == (ssize_t)size;
}
static int p_seek(void *f, uint64_t off) { return !dead && lseek(FD(f), (off_t)off, SEEK_SET) >= 0; }
static int p_size(void *f, uint64_t *size)
{
    struct stat st;
    if (dead || fstat(FD(f), &st)) return 0;
    *size = (uint64_t)st.st_size;
    return 1;
}
static int p_truncate(void *f, uint64_t size) { return alive_op() && ftruncate(FD(f), (off_t)size) == 0; }
static int p_flush(void *f) { return alive_op() && fsync(FD(f)) == 0; }
static void p_close(void *f) { close(FD(f)); }
static int p_rename(void *u, const char *from, const char *to)
{
    char a[600], b[600];
    (void)u;
    if (!alive_op()) return 0;
    native(a, from);
    native(b, to);
    return rename(a, b) == 0;
}
static int p_make_parents(void *u, const char *path)
{
    char n[600];
    size_t i;
    (void)u;
    if (dead) return 0;
    writes++;
    native(n, path);
    for (i = strlen(root) + 1; n[i]; i++) {
        if (n[i] != '/') continue;
        n[i] = 0;
        if (mkdir(n, 0755) && errno != EEXIST) return 0;
        n[i] = '/';
    }
    return 1;
}
static int p_remove(void *u, const char *path)
{
    char n[600];
    (void)u;
    if (!alive_op()) return 0;
    native(n, path);
    return unlink(n) == 0 || errno == ENOENT;
}
static int p_free(void *u, uint64_t *bytes)
{
    (void)u;
    *bytes = fake_free;
    return 1;
}

static install_probe_result p_probe(void *u, const char *path)
{
    char n[600];
    (void)u;
    if (dead || (locked && strcmp(path, locked) == 0 && locked_probe_fails)) return INSTALL_PROBE_ERROR;
    native(n, path);
    if (access(n, F_OK) == 0) return INSTALL_PROBE_EXISTS;
    return errno == ENOENT ? INSTALL_PROBE_MISSING : INSTALL_PROBE_ERROR;
}

static const install_fs posix_fs = {0, p_open, p_read, p_write, p_seek, p_size, p_truncate, p_flush, p_close,
                                    p_rename, p_make_parents, p_remove, p_free, p_probe};

/* ---- in-memory blob store ---- */

typedef struct {
    uint8_t sha[32];
    const uint8_t *data;
    size_t size;
} blob;
static blob blobs[64];
static int blob_count, fetches_left = -1, corrupt_fetch = -1, fetch_count;

static void add_blob(const uint8_t *data, size_t size, uint8_t sha[32])
{
    sha256(data, size, sha);
    memcpy(blobs[blob_count].sha, sha, 32);
    blobs[blob_count].data = data;
    blobs[blob_count].size = size;
    blob_count++;
}

static install_net_result n_fetch(void *u, const uint8_t sha[32], uint64_t blob_size, uint64_t offset, uint64_t length,
                                  install_sink sink, void *su)
{
    static uint8_t piece[777];
    int i, call = fetch_count++;
    (void)u;
    if (dead) return INSTALL_NET_FAILED;
    for (i = 0; i < blob_count; i++) {
        uint64_t sent = 0;
        if (memcmp(blobs[i].sha, sha, 32) || blobs[i].size != blob_size || offset + length > blob_size) continue;
        while (sent < length) {
            size_t n = length - sent < sizeof piece ? (size_t)(length - sent) : sizeof piece;
            if (fetches_left == 0 && sent >= length / 2) { /* killed mid-transfer */
                dead = 1;
                return INSTALL_NET_FAILED;
            }
            memcpy(piece, blobs[i].data + offset + sent, n);
            if (call == corrupt_fetch) piece[0] ^= 1;
            if (!sink(su, piece, n)) return INSTALL_NET_ABORTED;
            sent += n;
        }
        if (fetches_left > 0) fetches_left--;
        return INSTALL_NET_OK;
    }
    return INSTALL_NET_FAILED;
}
static const install_net mem_net = {0, n_fetch};

/* ---- fixtures ---- */

#define CHUNK 1000
static uint8_t big_a[3500], tail_a[1300], tail_b[200], result_a[4800], result_b[3700];
static uint8_t sha_big[32], sha_c[4][32], sha_tail_a[32], sha_tail_b[32], sha_res_a[32], sha_res_b[32], sha_empty[32];
static char text[16384];
static json_token tokens[4096];
static release_manifest manifest;
static install_result result;
static const install_ctx ctx = {&posix_fs, &mem_net, 0, 0, 0, "launcher.exe"};

static void hex(const uint8_t *sha, char out[65]) { hex_encode(sha, 32, out); }

static void setup_data(void)
{
    size_t i;
    uint32_t x = 12345;
    for (i = 0; i < sizeof big_a; i++) big_a[i] = (uint8_t)((x = x * 1103515245u + 12345u) >> 16);
    for (i = 0; i < sizeof tail_a; i++) tail_a[i] = (uint8_t)(i * 7);
    for (i = 0; i < sizeof tail_b; i++) tail_b[i] = (uint8_t)(i * 13 + 1);
    blob_count = 0;
    add_blob(big_a, sizeof big_a, sha_big);
    for (i = 0; i < 4; i++) add_blob(big_a + i * CHUNK, i < 3 ? CHUNK : sizeof big_a - 3 * CHUNK, sha_c[i]);
    add_blob(tail_a, sizeof tail_a, sha_tail_a);
    add_blob(tail_b, sizeof tail_b, sha_tail_b);
    sha256("", 0, sha_empty);
    /* release A: big + tail_a, writes "ABCD"@10 and "EF"@3000. Release B: big + tail_b, "xyz"@20. */
    memcpy(result_a, big_a, sizeof big_a);
    memcpy(result_a + sizeof big_a, tail_a, sizeof tail_a);
    memcpy(result_a + 10, "\xab\xcd", 2);
    memcpy(result_a + 3000, "\xef", 1);
    sha256(result_a, sizeof result_a, sha_res_a);
    memcpy(result_b, big_a, sizeof big_a);
    memcpy(result_b + sizeof big_a, tail_b, sizeof tail_b);
    memcpy(result_b + 20, "\x01\x02\x03", 3);
    sha256(result_b, sizeof result_b, sha_res_b);
}

/* Release "A" or "B": file data/big.pak (4 chunks), patch of it, empty file, line on cfg.ini. */
static void make_manifest(char release, int with_patch)
{
    char b[65], c[4][65], t[65], r[65], e[65], ops[8192];
    int i, n;
    hex(sha_big, b);
    for (i = 0; i < 4; i++) hex(sha_c[i], c[i]);
    hex(release == 'A' ? sha_tail_a : sha_tail_b, t);
    hex(release == 'A' ? sha_res_a : sha_res_b, r);
    hex(sha_empty, e);
    n = snprintf(ops, sizeof ops,
                 "{\"op\":\"file\",\"path\":\"data/big.pak\",\"sha256\":\"%s\",\"size\":3500,\"chunks\":["
                 "{\"sha256\":\"%s\",\"size\":1000},{\"sha256\":\"%s\",\"size\":1000},{\"sha256\":\"%s\",\"size\":1000},"
                 "{\"sha256\":\"%s\",\"size\":500}]},"
                 "{\"op\":\"file\",\"path\":\"empty.dat\",\"sha256\":\"%s\",\"size\":0,\"chunks\":[]},"
                 "{\"op\":\"line\",\"path\":\"cfg.ini\",\"prefix\":\"Server=\",\"line\":\"Server=%c.example\"}",
                 b, c[0], c[1], c[2], c[3], e, release);
    if (with_patch)
        snprintf(ops + n, sizeof ops - (size_t)n,
                 ",{\"op\":\"patch\",\"path\":\"data/big.pak\",\"base_sha256\":\"%s\",\"base_size\":3500,"
                 "\"writes\":[%s],\"append\":{\"sha256\":\"%s\",\"size\":%d},\"result_sha256\":\"%s\",\"result_size\":%d}",
                 b, release == 'A' ? "{\"offset\":10,\"data\":\"abcd\"},{\"offset\":3000,\"data\":\"ef\"}"
                                   : "{\"offset\":20,\"data\":\"010203\"}",
                 t, release == 'A' ? 1300 : 200, r, release == 'A' ? 4800 : 3700);
    snprintf(text, sizeof text, "{\"schema\":1,\"version\":\"rel-%c\",\"ops\":[%s]}", release, ops);
    CHECK(manifest_parse(&manifest, text, strlen(text), tokens, 4096, "launcher.exe", 0) == MANIFEST_OK);
}

static void fresh_root(void)
{
    char cmd[300];
    if (root[0]) {
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
        if (system(cmd)) {}
    }
    strcpy(root, "/tmp/launcher-test-XXXXXX");
    CHECK(mkdtemp(root) != 0);
    dead = 0;
    ops_left = fetches_left = corrupt_fetch = -1;
    fetch_count = 0;
    locked = 0;
    locked_probe_fails = 0;
}

static int file_has(const char *path, const uint8_t *data, size_t size)
{
    static uint8_t buf[8192];
    char n[600];
    FILE *f;
    size_t got;
    native(n, path);
    f = fopen(n, "rb");
    if (!f) return 0;
    got = fread(buf, 1, sizeof buf, f);
    fclose(f);
    return got == size && memcmp(buf, data, size) == 0;
}

static int exists(const char *path)
{
    char n[600];
    native(n, path);
    return access(n, F_OK) == 0;
}

static void write_file(const char *path, const void *data, size_t size)
{
    char n[600];
    FILE *f;
    p_make_parents(0, path);
    native(n, path);
    f = fopen(n, "wb");
    fwrite(data, 1, size, f);
    fclose(f);
}

static void check_release_a(void)
{
    CHECK(file_has("data/big.pak", result_a, sizeof result_a));
    CHECK(file_has("empty.dat", (const uint8_t *)"", 0));
    CHECK(file_has("cfg.ini", (const uint8_t *)"Server=A.example\r\n", 18));
    CHECK(!exists("data/big.pak.part"));
}

/* ---- tests ---- */

TEST(fresh_install_then_fast_path)
{
    fresh_root();
    make_manifest('A', 1);
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
    check_release_a();
    CHECK_EQ_U64(result.downloaded, 3500 + 1300);
    CHECK(install_is_current(&ctx, "rel-A"));
    CHECK(!install_is_current(&ctx, "rel-B"));
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
    CHECK(result.fast_path);
    CHECK_EQ_U64(result.downloaded, 0);
}

/* Dies at every mutating file operation in turn; a second run must always finish release A. */
TEST(crash_at_every_file_operation_recovers)
{
    int n, finished = 0;
    make_manifest('A', 1);
    for (n = 0; !finished && n < 400; n++) {
        fresh_root();
        ops_left = n;
        finished = install_run(&ctx, &manifest, &result) == INSTALL_OK && !dead;
        dead = 0;
        ops_left = -1;
        if (install_run(&ctx, &manifest, &result) != INSTALL_OK) {
            fprintf(stderr, "crash at op %d: rerun failed: %s (%s)\n", n, install_error_text(result.error), result.path);
            CHECK(0);
            break;
        }
        check_release_a();
    }
    CHECK(finished);
    CHECK(n > 20);
}

/* Dies in the middle of every download (each chunk, then the append): resumes without
 * fetching again what was complete. */
TEST(crash_in_every_download_resumes)
{
    int n;
    make_manifest('A', 1);
    for (n = 0; n < 5; n++) {
        fresh_root();
        fetches_left = n;
        CHECK(install_run(&ctx, &manifest, &result) != INSTALL_OK);
        dead = 0;
        fetches_left = -1;
        CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
        check_release_a();
        /* Chunks before the crash are not fetched again. */
        if (n < 4) CHECK(result.downloaded <= 3500 - (uint64_t)n * CHUNK + 1300);
    }
}

TEST(crash_between_append_and_writes_and_between_writes)
{
    /* The patch phase: journal written, then the append, then two writes. Kill after the
     * append's write and after the first in-place write by counting from the end. */
    int n, total;
    make_manifest('A', 1);
    fresh_root();
    ops_left = 1000000;
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
    total = 1000000 - ops_left;
    for (n = total - 12; n < total; n++) {
        fresh_root();
        ops_left = n;
        install_run(&ctx, &manifest, &result);
        dead = 0;
        ops_left = -1;
        CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
        check_release_a();
    }
}

/* Same as above, for the A -> B update: every crash while undoing A's journal (restore, hash,
 * save, drop) or applying B must leave a state the next run finishes. */
TEST(crash_at_every_file_operation_of_an_update_recovers)
{
    int n, finished = 0;
    for (n = 0; !finished && n < 400; n++) {
        fresh_root();
        make_manifest('A', 1);
        CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
        make_manifest('B', 1);
        ops_left = n;
        finished = install_run(&ctx, &manifest, &result) == INSTALL_OK && !dead;
        dead = 0;
        ops_left = -1;
        if (install_run(&ctx, &manifest, &result) != INSTALL_OK) {
            fprintf(stderr, "crash at op %d: rerun failed: %s (%s)\n", n, install_error_text(result.error), result.path);
            CHECK(0);
            break;
        }
        CHECK(file_has("data/big.pak", result_b, sizeof result_b));
        CHECK(file_has("cfg.ini", (const uint8_t *)"Server=B.example\r\n", 18));
    }
    CHECK(finished);
    CHECK(n > 10);
}

static size_t journal_files(void)
{
    char pattern[600];
    glob_t g;
    size_t n;
    native(pattern, MANIFEST_STATE_DIR "/j-*.jnl");
    if (glob(pattern, 0, 0, &g)) return 0;
    n = g.gl_pathc;
    globfree(&g);
    return n;
}

static size_t read_native(const char *path, uint8_t *buf, size_t cap)
{
    char n[600];
    FILE *f;
    size_t got;
    native(n, path);
    f = fopen(n, "rb");
    if (!f) return 0;
    got = fread(buf, 1, cap, f);
    fclose(f);
    return got;
}

/* A state.json that exists but cannot be read now (locked) stops the run before anything is
 * written; once readable again its journals still take the update through the base. */
TEST(unreadable_state_writes_nothing)
{
    static uint8_t before[16384], after[16384];
    size_t len;
    int pass;
    fresh_root();
    make_manifest('A', 1);
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
    len = read_native(INSTALL_STATE_FILE, before, sizeof before);
    CHECK(len > 0);
    make_manifest('B', 1);
    for (pass = 0; pass < 2; pass++) { /* exists but cannot be opened; existence unknown */
        locked = INSTALL_STATE_FILE;
        locked_probe_fails = pass;
        writes = 0;
        CHECK(install_run(&ctx, &manifest, &result) == INSTALL_ERR_IO);
        CHECK_STR(result.path, INSTALL_STATE_FILE);
        CHECK_EQ_U64(result.downloaded, 0);
        CHECK(!install_is_current(&ctx, "rel-A"));
        CHECK(!install_is_interrupted(&ctx));
        CHECK_EQ_U64(writes, 0);
    }
    locked = 0;
    CHECK(read_native(INSTALL_STATE_FILE, after, sizeof after) == len && memcmp(before, after, len) == 0);
    CHECK(!exists(INSTALL_STATE_BAD));
    check_release_a();
    CHECK(install_is_current(&ctx, "rel-A"));
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
    CHECK(file_has("data/big.pak", result_b, sizeof result_b));
    CHECK_EQ_U64(result.downloaded, 200); /* A's journal survived: only B's tail */
}

/* A corrupt state.json is kept aside as state.json.bad (replacing an older one) and the
 * install goes on from an empty state. */
TEST(corrupt_state_is_kept_as_bad)
{
    fresh_root();
    make_manifest('A', 1);
    write_file(INSTALL_STATE_BAD, "old", 3);
    write_file(INSTALL_STATE_FILE, "{\"schema\":1,\"files\":[", 22);
    CHECK(!install_is_current(&ctx, "rel-A"));
    CHECK(!install_is_interrupted(&ctx));
    CHECK(file_has(INSTALL_STATE_FILE, (const uint8_t *)"{\"schema\":1,\"files\":[", 22)); /* queries never rename */
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
    CHECK(result.state_reset);
    CHECK(file_has(INSTALL_STATE_BAD, (const uint8_t *)"{\"schema\":1,\"files\":[", 22));
    check_release_a();
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
    CHECK(result.fast_path);
    CHECK(!result.state_reset);
}

/* A patched file edited outside the launcher (bytes the journal does not cover): undoing the
 * journal does not give back the base, so the entry is forgotten instead of trusted. */
TEST(restored_file_edited_outside_is_forgotten)
{
    static uint8_t edited[sizeof result_a], base_edited[sizeof big_a];
    static char only[16384];
    int with_file;
    for (with_file = 1; with_file >= 0; with_file--) {
        fresh_root();
        make_manifest('A', 1);
        CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
        memcpy(edited, result_a, sizeof edited);
        edited[500] ^= 0x5a;
        write_file("data/big.pak", edited, sizeof edited);
        memcpy(base_edited, big_a, sizeof base_edited);
        base_edited[500] ^= 0x5a;
        make_manifest('B', 1);
        if (with_file) { /* the file op downloads it again, then the patch applies */
            CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
            CHECK(file_has("data/big.pak", result_b, sizeof result_b));
            CHECK_EQ_U64(result.downloaded, 3500 + 200);
        } else { /* patch only: refused, the restored file is left as it is */
            snprintf(only, sizeof only, "{\"schema\":1,\"version\":\"rel-X\",\"ops\":[%s",
                     strstr(text, "{\"op\":\"patch\""));
            CHECK(manifest_parse(&manifest, only, strlen(only), tokens, 4096, "launcher.exe", 0) == MANIFEST_OK);
            CHECK(install_run(&ctx, &manifest, &result) == INSTALL_ERR_BASE);
            CHECK(file_has("data/big.pak", base_edited, sizeof base_edited));
            CHECK(journal_files() == 0);
        }
    }
}

TEST(wrong_base_is_refused_without_touching)
{
    static uint8_t other[3500];
    static char only[16384];
    make_manifest('A', 1);
    snprintf(only, sizeof only, "{\"schema\":1,\"version\":\"rel-X\",\"ops\":[%s", strstr(text, "{\"op\":\"patch\""));
    CHECK(manifest_parse(&manifest, only, strlen(only), tokens, 4096, "launcher.exe", 0) == MANIFEST_OK);
    fresh_root();
    memset(other, 7, sizeof other);
    write_file("data/big.pak", other, sizeof other);
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_ERR_BASE);
    CHECK(file_has("data/big.pak", other, sizeof other));
    CHECK_EQ_U64(result.downloaded, 0);
    write_file("data/big.pak", other, 100); /* wrong size: refused without hashing */
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_ERR_BASE);
}

TEST(corrupt_chunk_fails_and_no_final_file)
{
    make_manifest('A', 0);
    fresh_root();
    corrupt_fetch = 1;
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_ERR_CORRUPT);
    CHECK_STR(result.path, "data/big.pak");
    CHECK(!exists("data/big.pak"));
    corrupt_fetch = -1;
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
    CHECK(file_has("data/big.pak", big_a, sizeof big_a));
}

TEST(release_a_to_b_goes_through_the_base)
{
    make_manifest('A', 1);
    fresh_root();
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
    make_manifest('B', 1);
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
    CHECK(file_has("data/big.pak", result_b, sizeof result_b));
    CHECK(file_has("cfg.ini", (const uint8_t *)"Server=B.example\r\n", 18));
    CHECK_EQ_U64(result.downloaded, 200); /* only the new tail: the base came back from the journal */
    make_manifest('A', 1);
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
    check_release_a();
}

TEST(existing_files_are_hashed_once_and_kept)
{
    make_manifest('A', 0);
    fresh_root();
    write_file("data/big.pak", big_a, sizeof big_a);
    write_file("cfg.ini", "a=1\nServer=old \r\n", 17);
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
    CHECK_EQ_U64(result.downloaded, 0);
    CHECK(file_has("cfg.ini", (const uint8_t *)"a=1\nServer=A.example\r\n", 22));
}

TEST(not_enough_space)
{
    make_manifest('A', 1);
    fresh_root();
    fake_free = 4000;
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_ERR_SPACE);
    CHECK_EQ_U64(result.need, 3500 + 1300 + INSTALL_SPACE_MARGIN);
    CHECK(!exists("data/big.pak.part"));
    fake_free = (uint64_t)1 << 40;
}

static int cancel_flag;
static void cancel_on_download(void *u, const install_progress *p)
{
    (void)u;
    if (p->phase == INSTALL_PHASE_DOWNLOAD && p->done >= 1500) cancel_flag = 1;
}

TEST(cancel_keeps_progress)
{
    install_ctx c = ctx;
    c.progress = cancel_on_download;
    c.cancel = &cancel_flag;
    make_manifest('A', 1);
    fresh_root();
    CHECK(!install_is_interrupted(&ctx));
    cancel_flag = 0;
    CHECK(install_run(&c, &manifest, &result) == INSTALL_ERR_CANCELLED);
    CHECK(install_is_interrupted(&ctx));
    cancel_flag = 0;
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
    CHECK(!install_is_interrupted(&ctx));
    CHECK(result.downloaded < 3500 + 1300);
    check_release_a();
}

/* ---- files no longer in the release ---- */

static char many_text[600000];
static json_token many_tokens[40000];

/* Release `version` with n empty files "<dir>/<i>" plus the extra ops (JSON, may be ""). */
static void make_many(const char *version, char dir, int n, const char *extra)
{
    char e[65];
    size_t k;
    int i;
    hex(sha_empty, e);
    k = (size_t)snprintf(many_text, sizeof many_text, "{\"schema\":1,\"version\":\"%s\",\"ops\":[", version);
    for (i = 0; i < n; i++)
        k += (size_t)snprintf(many_text + k, sizeof many_text - k,
                              "%s{\"op\":\"file\",\"path\":\"%c/%d\",\"sha256\":\"%s\",\"size\":0,\"chunks\":[]}",
                              i ? "," : "", dir, i, e);
    snprintf(many_text + k, sizeof many_text - k, "%s%s]}", n && extra[0] ? "," : "", extra);
    CHECK(manifest_parse(&manifest, many_text, strlen(many_text), many_tokens, 40000, "launcher.exe", 0) ==
          MANIFEST_OK);
}

/* Occurrences of needle in .launcher/state.json. */
static int state_count(const char *needle)
{
    static char buf[1 << 20];
    char n[600];
    const char *p;
    FILE *f;
    size_t got;
    int count = 0;
    native(n, INSTALL_STATE_FILE);
    f = fopen(n, "rb");
    if (!f) return -1;
    got = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[got] = 0;
    for (p = buf; (p = strstr(p, needle)) != 0; p++) count++;
    return count;
}

/* Two releases with disjoint paths that together exceed the state's capacity. */
TEST(orphans_keep_state_within_capacity)
{
    _Static_assert(2 * 2100 > STATE_FILES_MAX, "r1 + r2 must not fit in the state");
    fresh_root();
    make_many("r1", 'a', 2100, "");
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
    CHECK(exists("a/0") && exists("a/2099"));
    make_many("r2", 'b', 2100, "");
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
    CHECK(!exists("a/0") && !exists("a/1050") && !exists("a/2099"));
    CHECK(exists("b/0") && exists("b/2099"));
    CHECK(install_is_current(&ctx, "r2"));
    CHECK(state_count("\"a/") == 0);
    CHECK(state_count("\"b/") == 2100);
}

TEST(orphans_deleted_user_files_kept)
{
    fresh_root();
    make_many("r1", 'a', 4, "");
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
    write_file("a/user.sav", "save", 4);    /* never written by the launcher */
    write_file("a/1.part", "half", 4);      /* abandoned download of an orphan */
    write_file("a/2", "changed", 7);        /* managed, but rewritten by the game */
    write_file("a/3.part", "x", 1);         /* stray temporary of an orphan (".part" paths are never managed) */
    make_many("r2", 'b', 2, "");
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
    CHECK(!exists("a/0") && !exists("a/1") && !exists("a/1.part") && !exists("a/3") && !exists("a/3.part"));
    CHECK(file_has("a/user.sav", (const uint8_t *)"save", 4));
    CHECK(file_has("a/2", (const uint8_t *)"changed", 7));
    CHECK(exists("b/0") && exists("b/1"));
    CHECK(state_count("\"a/2\"") == 0);
    CHECK(install_is_current(&ctx, "r2"));
}

/* Dies at every mutating file operation of an update that deletes orphans; the next run must
 * finish it without touching the user's file. */
TEST(crash_while_deleting_orphans_recovers)
{
    int n, finished = 0;
    for (n = 0; !finished && n < 200; n++) {
        fresh_root();
        make_many("r1", 'a', 5, "");
        CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
        write_file("a/user.sav", "save", 4);
        write_file("a/4.part", "half", 4);
        make_many("r2", 'b', 3, "");
        ops_left = n;
        finished = install_run(&ctx, &manifest, &result) == INSTALL_OK && !dead;
        dead = 0;
        ops_left = -1;
        if (install_run(&ctx, &manifest, &result) != INSTALL_OK) {
            fprintf(stderr, "crash at op %d: rerun failed: %s (%s)\n", n, install_error_text(result.error), result.path);
            CHECK(0);
            break;
        }
        CHECK(!exists("a/0") && !exists("a/4") && !exists("a/4.part"));
        CHECK(exists("b/0") && exists("b/2"));
        CHECK(file_has("a/user.sav", (const uint8_t *)"save", 4));
        CHECK(state_count("\"a/") == 0);
    }
    CHECK(finished);
    CHECK(n > 8);
}

/* A file the user (or the game) already had and the launcher only edited with a line op is
 * not the launcher's: when a release stops mentioning it, it is forgotten, never deleted. */
TEST(orphan_line_edit_of_user_file_is_kept)
{
    fresh_root();
    write_file("cfg/settings.ini", "a=0\r\n", 5);
    make_many("r1", 'a', 1, "{\"op\":\"line\",\"path\":\"cfg/settings.ini\",\"prefix\":\"K=\",\"line\":\"K=1\"}");
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
    CHECK(file_has("cfg/settings.ini", (const uint8_t *)"a=0\r\nK=1\r\n", 10));
    CHECK(state_count("\"owned\": true") == 1); /* a/0 only */
    write_file("cfg/settings.ini.part", "tmp", 3);
    make_many("r2", 'a', 1, "");
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
    CHECK(file_has("cfg/settings.ini", (const uint8_t *)"a=0\r\nK=1\r\n", 10));
    CHECK(!exists("cfg/settings.ini.part")); /* the launcher's own temporary */
    CHECK(state_count("settings.ini") == 0);
    CHECK(exists("a/0"));
}

/* Delivered whole by a file op, then edited by a line op (r1) or a patch (A, B): still the
 * launcher's, so it is deleted once a release drops it. A line op alone (cfg.ini) is not. */
TEST(orphan_delivered_then_edited_is_deleted)
{
    fresh_root();
    make_many("r1", 'a', 2, "{\"op\":\"line\",\"path\":\"a/1\",\"prefix\":\"K=\",\"line\":\"K=1\"}");
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
    CHECK(file_has("a/1", (const uint8_t *)"K=1\r\n", 5));
    make_many("r2", 'b', 1, "");
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
    CHECK(!exists("a/0") && !exists("a/1"));

    fresh_root();
    make_manifest('A', 1);
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
    make_manifest('B', 1);
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
    CHECK(state_count("\"owned\": true") == 2); /* data/big.pak (patched) and empty.dat */
    make_many("r3", 'b', 1, "");
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
    CHECK(!exists("data/big.pak") && !exists("empty.dat"));
    CHECK(file_has("cfg.ini", (const uint8_t *)"Server=B.example\r\n", 18));
    CHECK(state_count("\"data/") == 0 && state_count("cfg.ini") == 0);
}

/* A state.json written before "owned" existed still loads; its orphans are only forgotten. */
TEST(orphans_of_legacy_state_are_only_forgotten)
{
    static char buf[1 << 16], out[1 << 16];
    char n[600];
    const char *p, *hit;
    size_t got, k = 0;
    FILE *f;
    fresh_root();
    make_many("r1", 'a', 2, "");
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
    native(n, INSTALL_STATE_FILE);
    f = fopen(n, "rb");
    got = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[got] = 0;
    for (p = buf; (hit = strstr(p, ", \"owned\": true")) != 0; p = hit + 15) {
        memcpy(out + k, p, (size_t)(hit - p));
        k += (size_t)(hit - p);
    }
    k += (size_t)snprintf(out + k, sizeof out - k, "%s", p);
    write_file(INSTALL_STATE_FILE, out, k);
    CHECK(state_count("owned") == 0);
    CHECK(install_is_current(&ctx, "r1")); /* still loads */
    make_many("r2", 'b', 1, "");
    CHECK(install_run(&ctx, &manifest, &result) == INSTALL_OK);
    CHECK(exists("a/0") && exists("a/1"));
    CHECK(state_count("\"a/") == 0);
    CHECK(exists("b/0") && state_count("\"owned\": true") == 1);
}

TEST(state_owned_round_trip)
{
    static install_state s;
    static char out[4096];
    static const char legacy[] = "{\"schema\": 1, \"release\": \"r\", \"files\": [{\"path\": \"x\", \"sha256\": \""
                                 "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\", \"size\": 0}]}";
    static const char bad[] = "{\"schema\": 1, \"release\": \"r\", \"files\": [{\"path\": \"x\", \"sha256\": \""
                              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\", \"size\": 0, "
                              "\"owned\": 1}]}";
    size_t len;
    state_clear(&s);
    CHECK(state_set(&s, "edited.ini", sha_empty, 0));
    CHECK(state_set_owned(&s, "game.pak", sha_big, 3500, 1));
    CHECK(state_set(&s, "game.pak", sha_res_a, 4800)); /* an edit keeps the owner */
    CHECK(state_find(&s, "game.pak")->owned == 1 && state_find(&s, "edited.ini")->owned == 0);
    len = state_write(&s, out, sizeof out);
    CHECK(len > 0);
    out[len] = 0;
    CHECK(strstr(out, "\"size\": 4800, \"owned\": true}") != 0);
    CHECK(strstr(out, "\"size\": 0}") != 0);
    CHECK(state_parse(&s, out, len, tokens, 4096));
    CHECK(s.file_count == 2);
    CHECK(state_find(&s, "game.pak")->owned == 1 && state_find(&s, "edited.ini")->owned == 0);
    CHECK(state_matches(&s, "game.pak", sha_res_a, 4800));
    CHECK(state_set_owned(&s, "game.pak", sha_res_a, 4800, 0) && state_find(&s, "game.pak")->owned == 0);
    CHECK(state_parse(&s, legacy, sizeof legacy - 1, tokens, 4096));
    CHECK(s.file_count == 1 && state_find(&s, "x")->owned == 0);
    CHECK(!state_parse(&s, bad, sizeof bad - 1, tokens, 4096) && s.file_count == 0);
}

/* Same cases as the reference applier of the contract (apply_line). */
static const char *line_case(const char *in, const char *prefix, const char *line)
{
    static uint8_t out[512];
    size_t n = 0;
    CHECK(line_apply((const uint8_t *)in, strlen(in), prefix, strlen(prefix), line, strlen(line), out, sizeof out - 1, &n));
    out[n] = 0;
    return (const char *)out;
}

TEST(line_semantics)
{
    CHECK_STR(line_case("", "K=", "K=1"), "K=1\r\n");
    CHECK_STR(line_case("a=0", "K=", "K=1"), "a=0\r\nK=1\r\n");
    CHECK_STR(line_case("a=0\n", "K=", "K=1"), "a=0\nK=1\r\n");
    CHECK_STR(line_case("a=0\r", "K=", "K=1"), "a=0\r\r\nK=1\r\n");
    CHECK_STR(line_case("a\r\n  K=old\nK=2\r\n", "K=", "K=1"), "a\r\nK=1\nK=2\r\n");
    CHECK_STR(line_case("\t\x0bK=x\rz", "K=", "K=1"), "K=1\rz");
    CHECK_STR(line_case("a\nK=last", "K=", "K=1"), "a\nK=1");
    CHECK_STR(line_case("k=lower\r\n", "K=", "K=1"), "k=lower\r\nK=1\r\n");
    CHECK_STR(line_case("x\r\n\r\nK= v\r\n", "K=", "K=1"), "x\r\n\r\nK=1\r\n");
}

int main(void)
{
    setup_data();
    RUN(fresh_install_then_fast_path);
    RUN(crash_at_every_file_operation_recovers);
    RUN(crash_in_every_download_resumes);
    RUN(crash_between_append_and_writes_and_between_writes);
    RUN(crash_at_every_file_operation_of_an_update_recovers);
    RUN(unreadable_state_writes_nothing);
    RUN(corrupt_state_is_kept_as_bad);
    RUN(restored_file_edited_outside_is_forgotten);
    RUN(wrong_base_is_refused_without_touching);
    RUN(corrupt_chunk_fails_and_no_final_file);
    RUN(release_a_to_b_goes_through_the_base);
    RUN(existing_files_are_hashed_once_and_kept);
    RUN(not_enough_space);
    RUN(cancel_keeps_progress);
    RUN(orphans_keep_state_within_capacity);
    RUN(orphans_deleted_user_files_kept);
    RUN(crash_while_deleting_orphans_recovers);
    RUN(orphan_line_edit_of_user_file_is_kept);
    RUN(orphan_delivered_then_edited_is_deleted);
    RUN(orphans_of_legacy_state_are_only_forgotten);
    RUN(state_owned_round_trip);
    RUN(line_semantics);
    fresh_root();
    {
        char cmd[300];
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
        if (system(cmd)) {}
    }
    return test_report();
}
