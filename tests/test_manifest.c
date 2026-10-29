#include "testlib.h"
#include "../src/core/manifest.h"

#define H64 "0000000000000000000000000000000000000000000000000000000000000000"
#define H64B "1111111111111111111111111111111111111111111111111111111111111111"

static json_token tokens[512];
static release_manifest m;
static char text[4096];
static int bad;

static manifest_error parse_ops(const char *ops)
{
    snprintf(text, sizeof text, "{\"schema\":1,\"version\":\"1.0\",\"ops\":[%s]}", ops);
    return manifest_parse(&m, text, strlen(text), tokens, 512, "Launcher.exe", &bad);
}

static manifest_error file_op(const char *path)
{
    char op[512];
    snprintf(op, sizeof op, "{\"op\":\"file\",\"path\":\"%s\",\"sha256\":\"" H64 "\",\"size\":0,\"chunks\":[]}", path);
    return parse_ops(op);
}

TEST(valid_manifest)
{
    CHECK(parse_ops("{\"op\":\"file\",\"path\":\"a/b.pak\",\"sha256\":\"" H64 "\",\"size\":12,\"chunks\":["
                    "{\"sha256\":\"" H64B "\",\"size\":10},{\"sha256\":\"" H64 "\",\"size\":2}]},"
                    "{\"op\":\"patch\",\"path\":\"a/b.pak\",\"base_sha256\":\"" H64 "\",\"base_size\":12,"
                    "\"writes\":[{\"offset\":10,\"data\":\"a1b2\"}],\"append\":{\"sha256\":\"" H64B "\",\"size\":5},"
                    "\"result_sha256\":\"" H64B "\",\"result_size\":17},"
                    "{\"op\":\"line\",\"path\":\"cfg.ini\",\"prefix\":\"K=\",\"line\":\"K=\\u00e9\"},"
                    "{\"op\":\"line\",\"path\":\"cfg.ini\",\"prefix\":\"J=\",\"line\":\"J=1\"}") == MANIFEST_OK);
    CHECK_STR(m.version, "1.0");
    CHECK_EQ_U64(m.op_count, 4);
    CHECK_EQ_U64(m.ops[0].kind, MANIFEST_OP_FILE);
    CHECK_EQ_U64(m.ops[0].count, 2);
    CHECK_EQ_U64(m.chunks[0].sha256[0], 0x11);
    CHECK_EQ_U64(m.ops[1].append.size, 5);
    CHECK_EQ_U64(m.writes[0].offset, 10);
    CHECK_EQ_U64(m.writes[0].len, 2);
    CHECK_EQ_U64(manifest_bytes(&m, m.writes[0].data)[1], 0xb2);
    CHECK_STR(manifest_str(&m, m.ops[2].line), "K=\xc3\xa9");
    CHECK_STR(manifest_str(&m, m.ops[2].path), "cfg.ini");
}

TEST(empty_file_is_valid)
{
    CHECK(file_op("empty.txt") == MANIFEST_OK);
    CHECK_EQ_U64(m.ops[0].count, 0);
}

TEST(unsafe_or_reserved_paths_reject_everything)
{
    static const char *const bad_paths[] = {"../x", "/abs", "a//b", "C:x", "a\\\\b", "a/./b", "CON", "nul.txt",
                                            "dir/", "x.", "launcher.json", "LAUNCHER.EXE", "launcher.log",
                                            ".launcher/state.json", ".LAUNCHER", "",
                                            "LAUNCH~1.EXE", "dir/PROGRA~2/x", "launcher.exe.new",
                                            "Launcher.EXE.NEW"};
    size_t i;
    for (i = 0; i < sizeof bad_paths / sizeof bad_paths[0]; i++) {
        if (file_op(bad_paths[i]) != MANIFEST_ERR_PATH) fprintf(stderr, "accepted: %s\n", bad_paths[i]);
        CHECK(file_op(bad_paths[i]) == MANIFEST_ERR_PATH);
        CHECK_EQ_U64(m.op_count, 0);
    }
    CHECK(file_op(".launcherx/a") == MANIFEST_OK);
    CHECK(file_op("a~b/c~.txt") == MANIFEST_OK); /* "~" alone is fine; only "~<digit>" aliases */
    CHECK(file_op("launcher.exe.new2") == MANIFEST_OK);
    /* One bad path among good ones rejects the whole manifest. */
    CHECK(parse_ops("{\"op\":\"line\",\"path\":\"ok.ini\",\"prefix\":\"a\",\"line\":\"b\"},"
                    "{\"op\":\"line\",\"path\":\"../bad.ini\",\"prefix\":\"a\",\"line\":\"b\"}") == MANIFEST_ERR_PATH);
    CHECK_EQ_U64(bad, 1);
    CHECK_EQ_U64(m.op_count, 0);
}

TEST(sizes_hashes_and_order)
{
    CHECK(parse_ops("{\"op\":\"file\",\"path\":\"a\",\"sha256\":\"" H64 "\",\"size\":3,\"chunks\":[]}") == MANIFEST_ERR_SIZE);
    CHECK(parse_ops("{\"op\":\"file\",\"path\":\"a\",\"sha256\":\"" H64 "\",\"size\":3,\"chunks\":[{\"sha256\":\"" H64
                    "\",\"size\":2}]}") == MANIFEST_ERR_SIZE);
    CHECK(parse_ops("{\"op\":\"file\",\"path\":\"a\",\"sha256\":\"" H64 "\",\"size\":0,\"chunks\":[{\"sha256\":\"" H64
                    "\",\"size\":0}]}") == MANIFEST_ERR_SIZE);
    CHECK(parse_ops("{\"op\":\"file\",\"path\":\"a\",\"sha256\":\"" H64 "\",\"size\":8388609,\"chunks\":[{\"sha256\":\"" H64
                    "\",\"size\":8388609}]}") == MANIFEST_ERR_SIZE);
    CHECK(parse_ops("{\"op\":\"file\",\"path\":\"a\",\"sha256\":\"ABC\",\"size\":0,\"chunks\":[]}") == MANIFEST_ERR_HASH);
    CHECK(parse_ops("{\"op\":\"patch\",\"path\":\"a\",\"base_sha256\":\"" H64 "\",\"base_size\":4,\"writes\":[{\"offset\":3,"
                    "\"data\":\"0102\"}],\"result_sha256\":\"" H64 "\",\"result_size\":4}") == MANIFEST_ERR_WRITE);
    CHECK(parse_ops("{\"op\":\"patch\",\"path\":\"a\",\"base_sha256\":\"" H64 "\",\"base_size\":4,\"writes\":[{\"offset\":0,"
                    "\"data\":\"0G\"}],\"result_sha256\":\"" H64 "\",\"result_size\":4}") == MANIFEST_ERR_WRITE);
    CHECK(parse_ops("{\"op\":\"patch\",\"path\":\"a\",\"base_sha256\":\"" H64 "\",\"base_size\":4,"
                    "\"result_sha256\":\"" H64 "\",\"result_size\":5}") == MANIFEST_ERR_SIZE);
    CHECK(parse_ops("{\"op\":\"line\",\"path\":\"a\",\"prefix\":\"x\",\"line\":\"y\\r\\nz\"}") == MANIFEST_ERR_LINE);
    CHECK(parse_ops("{\"op\":\"move\",\"path\":\"a\"}") == MANIFEST_ERR_OP);
    CHECK(parse_ops("{\"op\":\"line\",\"path\":\"a\",\"prefix\":\"x\",\"line\":\"y\"},"
                    "{\"op\":\"file\",\"path\":\"A\",\"sha256\":\"" H64 "\",\"size\":0,\"chunks\":[]}") == MANIFEST_ERR_ORDER);
    CHECK(parse_ops("{\"op\":\"patch\",\"path\":\"a\",\"base_sha256\":\"" H64 "\",\"base_size\":4,\"result_sha256\":\"" H64
                    "\",\"result_size\":4},{\"op\":\"line\",\"path\":\"a\",\"prefix\":\"x\",\"line\":\"y\"}") == MANIFEST_ERR_ORDER);
    snprintf(text, sizeof text, "{\"schema\":2,\"version\":\"1\",\"ops\":[]}");
    CHECK(manifest_parse(&m, text, strlen(text), tokens, 512, "x.exe", 0) == MANIFEST_ERR_SCHEMA);
    snprintf(text, sizeof text, "{\"schema\":1,\"version\":\"\",\"ops\":[]}");
    CHECK(manifest_parse(&m, text, strlen(text), tokens, 512, "x.exe", 0) == MANIFEST_ERR_VERSION);
}

#define LINE_OP(path) "{\"op\":\"line\",\"path\":\"" path "\",\"prefix\":\"a\",\"line\":\"b\"}"

TEST(part_suffix_is_reserved)
{
    CHECK(file_op("x.part") == MANIFEST_ERR_PATH);
    CHECK(file_op("dir/X.PART") == MANIFEST_ERR_PATH);
    CHECK(parse_ops(LINE_OP("x") "," LINE_OP("x.part")) == MANIFEST_ERR_PATH);
    CHECK_EQ_U64(bad, 1);
    CHECK(parse_ops(LINE_OP("X") "," LINE_OP("x.PART")) == MANIFEST_ERR_PATH);
    CHECK(file_op("x.parts") == MANIFEST_OK);
    CHECK(file_op("x.part/y") == MANIFEST_OK); /* only the last component's suffix counts */
    CHECK(file_op(".part") == MANIFEST_ERR_PATH);
}

TEST(path_cannot_be_file_and_folder)
{
    CHECK(parse_ops(LINE_OP("a") "," LINE_OP("a/b")) == MANIFEST_ERR_TREE);
    CHECK_EQ_U64(bad, 1);
    CHECK_EQ_U64(m.op_count, 0);
    CHECK(parse_ops(LINE_OP("a/b") "," LINE_OP("A")) == MANIFEST_ERR_TREE);
    CHECK(parse_ops(LINE_OP("x/a/b/c") "," LINE_OP("ok") "," LINE_OP("X/A")) == MANIFEST_ERR_TREE);
    CHECK_EQ_U64(bad, 2);
    /* Shared prefixes that are not whole components, and repeated paths, are fine. */
    CHECK(parse_ops(LINE_OP("a") "," LINE_OP("ab/c") "," LINE_OP("a.txt") "," LINE_OP("a") "," LINE_OP("b/a/c") ","
                    LINE_OP("b/ab")) == MANIFEST_OK);
    CHECK_EQ_U64(m.op_count, 6);
    /* Order rules still hold when other paths sit in between. */
    CHECK(parse_ops(LINE_OP("b") "," "{\"op\":\"patch\",\"path\":\"a\",\"base_sha256\":\"" H64 "\",\"base_size\":0,"
                    "\"result_sha256\":\"" H64 "\",\"result_size\":0}," LINE_OP("c") "," LINE_OP("A")) == MANIFEST_ERR_ORDER);
    CHECK_EQ_U64(bad, 3);
}

static char big_text[MANIFEST_OPS_MAX * 256];
static json_token big_tokens[MANIFEST_OPS_MAX * 16];

/* A manifest with n ops built by op(i, buf, cap) into big_text. */
static manifest_error parse_many(uint32_t n, int (*op)(uint32_t, char *, size_t))
{
    size_t len = (size_t)snprintf(big_text, sizeof big_text, "{\"schema\":1,\"version\":\"1\",\"ops\":[");
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (i) big_text[len++] = ',';
        len += (size_t)op(i, big_text + len, sizeof big_text - len);
    }
    len += (size_t)snprintf(big_text + len, sizeof big_text - len, "]}");
    return manifest_parse(&m, big_text, len, big_tokens, sizeof big_tokens / sizeof big_tokens[0], "l.exe", &bad);
}

static int patch_op(uint32_t i, char *buf, size_t cap)
{
    return snprintf(buf, cap, "{\"op\":\"patch\",\"path\":\"p%u\",\"base_sha256\":\"" H64 "\",\"base_size\":1,"
                    "\"result_sha256\":\"" H64 "\",\"result_size\":1}", i);
}

static int empty_file_op(uint32_t i, char *buf, size_t cap)
{
    return snprintf(buf, cap, "{\"op\":\"file\",\"path\":\"d%u/f%u.bin\",\"sha256\":\"" H64 "\",\"size\":0,"
                    "\"chunks\":[]}", i % 64, i);
}

TEST(patches_fit_the_journals)
{
    CHECK(parse_many(MANIFEST_PATCHES_MAX, patch_op) == MANIFEST_OK);
    CHECK_EQ_U64(m.op_count, MANIFEST_PATCHES_MAX);
    CHECK(parse_many(MANIFEST_PATCHES_MAX + 1, patch_op) == MANIFEST_ERR_LIMIT);
    CHECK_EQ_U64(bad, MANIFEST_PATCHES_MAX);
    CHECK_EQ_U64(m.op_count, 0);
}

TEST(full_manifest_of_distinct_paths)
{
    CHECK(parse_many(MANIFEST_OPS_MAX, empty_file_op) == MANIFEST_OK);
    CHECK_EQ_U64(m.op_count, MANIFEST_OPS_MAX);
}

/* 0x7fffffffffffffff + 0x7fffffffffffffff fits, one more byte overflows. A 0-chunk file must
 * have size 0, so the big sizes come from patch appends (base 0, result = append). */
#define BIG "9223372036854775807"
#define APPEND_OP(path, size) "{\"op\":\"patch\",\"path\":\"" path "\",\"base_sha256\":\"" H64 "\",\"base_size\":0," \
    "\"append\":{\"sha256\":\"" H64 "\",\"size\":" size "},\"result_sha256\":\"" H64 "\",\"result_size\":" size "}"

TEST(download_total_cannot_overflow)
{
    CHECK(parse_ops(APPEND_OP("a", BIG) "," APPEND_OP("b", BIG)) == MANIFEST_OK);
    CHECK(parse_ops(APPEND_OP("a", BIG) "," APPEND_OP("b", BIG) "," APPEND_OP("c", "2")) == MANIFEST_ERR_SIZE);
    CHECK_EQ_U64(bad, 2);
    /* A file op counts too: 1 byte on top of the maximum. */
    CHECK(parse_ops(APPEND_OP("a", BIG) "," APPEND_OP("b", "9223372036854775808") ","
                    "{\"op\":\"file\",\"path\":\"f\",\"sha256\":\"" H64 "\",\"size\":1,\"chunks\":["
                    "{\"sha256\":\"" H64 "\",\"size\":1}]}") == MANIFEST_ERR_SIZE);
    CHECK(manifest_error_text(MANIFEST_ERR_TREE)[0] != 'u'); /* has its own text */
}

/* The example release served by examples/dev-server stays valid. */
TEST(example_release_is_valid)
{
    static char path[1024], buf[1 << 16];
    const char *slash = strrchr(__FILE__, '/');
    FILE *f;
    size_t n;
    snprintf(path, sizeof path, "%.*s/../examples/dev-server/example-root/release.json",
             slash ? (int)(slash - __FILE__) : 1, slash ? __FILE__ : ".");
    f = fopen(path, "rb");
    CHECK(f != 0);
    if (!f) return;
    n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    CHECK(manifest_parse(&m, buf, n, big_tokens, sizeof big_tokens / sizeof big_tokens[0], "launcher.exe", &bad) == MANIFEST_OK);
}

int main(void)
{
    RUN(valid_manifest);
    RUN(empty_file_is_valid);
    RUN(unsafe_or_reserved_paths_reject_everything);
    RUN(sizes_hashes_and_order);
    RUN(part_suffix_is_reserved);
    RUN(path_cannot_be_file_and_folder);
    RUN(patches_fit_the_journals);
    RUN(full_manifest_of_distinct_paths);
    RUN(download_total_cannot_overflow);
    RUN(example_release_is_valid);
    return test_report();
}
