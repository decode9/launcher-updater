/*
 * test_install.c - Install engine over a POSIX file port (in a temporary folder) and an
 * in-memory blob store, with fault injection: "dying" at the n-th mutating file operation or
 * network fetch makes every later operation fail, as if the process had been killed there.
 */
#define _POSIX_C_SOURCE 200809L
#include "testlib.h"
#include "../src/core/install.h"
#include "../src/core/lineop.h"
#include "../src/core/str.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- fault-injecting POSIX port ---- */

static char root[256];
static int dead, ops_left = -1; /* ops_left: mutating ops until death (-1 never) */
static uint64_t fake_free = (uint64_t)1 << 40;

static int alive_op(void)
{
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
    if (dead) return 0;
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

static const install_fs posix_fs = {0, p_open, p_read, p_write, p_seek, p_size, p_truncate, p_flush, p_close,
                                    p_rename, p_make_parents, p_remove, p_free};

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
    RUN(wrong_base_is_refused_without_touching);
    RUN(corrupt_chunk_fails_and_no_final_file);
    RUN(release_a_to_b_goes_through_the_base);
    RUN(existing_files_are_hashed_once_and_kept);
    RUN(not_enough_space);
    RUN(cancel_keeps_progress);
    RUN(line_semantics);
    fresh_root();
    {
        char cmd[300];
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
        if (system(cmd)) {}
    }
    return test_report();
}
