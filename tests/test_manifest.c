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
                                            ".launcher/state.json", ".LAUNCHER", ""};
    size_t i;
    for (i = 0; i < sizeof bad_paths / sizeof bad_paths[0]; i++) {
        if (file_op(bad_paths[i]) != MANIFEST_ERR_PATH) fprintf(stderr, "accepted: %s\n", bad_paths[i]);
        CHECK(file_op(bad_paths[i]) == MANIFEST_ERR_PATH);
        CHECK_EQ_U64(m.op_count, 0);
    }
    CHECK(file_op(".launcherx/a") == MANIFEST_OK);
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

int main(void)
{
    RUN(valid_manifest);
    RUN(empty_file_is_valid);
    RUN(unsafe_or_reserved_paths_reject_everything);
    RUN(sizes_hashes_and_order);
    return test_report();
}
