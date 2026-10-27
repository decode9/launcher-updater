#include "testlib.h"
#include "../src/core/config.h"

#define PIN "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"

static json_token tokens[128];
static launcher_config cfg;
static char text[1024];

static config_error parse_with(const char *server, const char *port, const char *pin, const char *blobs,
                               const char *launch)
{
    snprintf(text, sizeof text, "{\"server\": %s, \"port\": %s, \"fingerprint\": %s, \"blobs_url\": %s, \"launch\": %s}",
             server, port, pin, blobs, launch);
    return config_parse(&cfg, text, strlen(text), tokens, 128);
}

static config_error parse_exe(const char *exe_json)
{
    char launch[512];
    snprintf(launch, sizeof launch, "{\"exe\": %s}", exe_json);
    return parse_with("\"play.example.com\"", "5103", "\"" PIN "\"", "null", launch);
}

TEST(valid)
{
    CHECK(parse_with("\"play.example.com\"", "5103", "\"" PIN "\"", "null",
                     "{\"exe\": \"bin/Game.exe\", \"args\": [\"0\", \"{server}\"]}") == CONFIG_OK);
    CHECK_STR(cfg.server, "play.example.com");
    CHECK_EQ_U64(cfg.port, 5103);
    CHECK_EQ_U64(cfg.fingerprint[0], 0x00);
    CHECK_EQ_U64(cfg.fingerprint[31], 0xff);
    CHECK(!cfg.has_blobs_url);
    CHECK_STR(cfg.launch_exe, "bin/Game.exe");
    CHECK_EQ_U64(cfg.launch_argc, 2);
    CHECK_STR(cfg.launch_args[1], "{server}");
}

TEST(optional_fields)
{
    const char *t = "{\"server\": \"10.0.0.5\", \"port\": 443, \"fingerprint\": \"" PIN "\", "
                    "\"launch\": {\"exe\": \"Game.exe\"}, \"future\": [1, 2]}";
    CHECK(config_parse(&cfg, t, strlen(t), tokens, 128) == CONFIG_OK);
    CHECK_EQ_U64(cfg.launch_argc, 0);
    CHECK(parse_with("\"a.b\"", "1", "\"" PIN "\"", "\"https://cdn.example.com/blobs\"", "{\"exe\": \"G.exe\"}") == CONFIG_OK);
    CHECK(cfg.has_blobs_url);
    CHECK_STR(cfg.blobs_url, "https://cdn.example.com/blobs");
}

TEST(bad_fields)
{
    const char *launch = "{\"exe\": \"Game.exe\"}";
    CHECK(parse_with("\"\"", "5103", "\"" PIN "\"", "null", launch) == CONFIG_ERR_SERVER);
    CHECK(parse_with("\"a b\"", "5103", "\"" PIN "\"", "null", launch) == CONFIG_ERR_SERVER);
    CHECK(parse_with("\"a/b\"", "5103", "\"" PIN "\"", "null", launch) == CONFIG_ERR_SERVER);
    CHECK(parse_with("5", "5103", "\"" PIN "\"", "null", launch) == CONFIG_ERR_SERVER);
    CHECK(parse_with("\"a\"", "0", "\"" PIN "\"", "null", launch) == CONFIG_ERR_PORT);
    CHECK(parse_with("\"a\"", "65536", "\"" PIN "\"", "null", launch) == CONFIG_ERR_PORT);
    CHECK(parse_with("\"a\"", "\"80\"", "\"" PIN "\"", "null", launch) == CONFIG_ERR_PORT);
    CHECK(parse_with("\"a\"", "80", "\"00\"", "null", launch) == CONFIG_ERR_FINGERPRINT);
    CHECK(parse_with("\"a\"", "80", "\"00112233445566778899AABBCCDDEEFF00112233445566778899aabbccddeeff\"", "null",
                     launch) == CONFIG_ERR_FINGERPRINT);
    CHECK(parse_with("\"a\"", "80", "\"" PIN "\"", "\"http://x\"", launch) == CONFIG_ERR_BLOBS_URL);
    CHECK(parse_with("\"a\"", "80", "\"" PIN "\"", "\"https://\"", launch) == CONFIG_ERR_BLOBS_URL);
    CHECK(parse_with("\"a\"", "80", "\"" PIN "\"", "null", "null") == CONFIG_ERR_LAUNCH);
    CHECK(parse_with("\"a\"", "80", "\"" PIN "\"", "null", "{\"exe\": \"G.exe\", \"args\": [1]}") == CONFIG_ERR_LAUNCH);
    CHECK(config_parse(&cfg, "[]", 2, tokens, 128) == CONFIG_ERR_JSON);
    CHECK(config_parse(&cfg, "{", 1, tokens, 128) == CONFIG_ERR_JSON);
}

TEST(launch_paths)
{
    static const char *const good[] = {"\"Game.exe\"", "\"bin/Game.exe\"", "\"a b/c.d.exe\"", "\"..a/b..c\"",
                                       "\"com10.txt\"", "\"\xc3\xb1/x\""};
    static const char *const bad[] = {
        "\"/Game.exe\"", "\"C:/Game.exe\"", "\"C:Game.exe\"", "\"bin\\\\Game.exe\"", "\"\\\\\\\\host\\\\x\"",
        "\"../Game.exe\"", "\"bin/../../Game.exe\"", "\"bin/..\"", "\"./Game.exe\"", "\"bin//Game.exe\"",
        "\"bin/\"", "\"\"", "\"Game.exe.\"", "\"Game.exe \"", "\"CON\"", "\"nul.txt\"", "\"bin/COM1.exe\"",
        "\"lpt9\"", "\"a\\u0001b\"", "\"a*b\"", "\"a?b\"", "\"a|b\"", "\"a<b\"", "\"a\\\"b\"", "null", "5",
    };
    size_t i;
    for (i = 0; i < sizeof good / sizeof good[0]; i++) {
        config_error e = parse_exe(good[i]);
        if (e != CONFIG_OK) fprintf(stderr, "rejected good path %s\n", good[i]);
        CHECK(e == CONFIG_OK);
    }
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        config_error e = parse_exe(bad[i]);
        if (e != CONFIG_ERR_LAUNCH) fprintf(stderr, "accepted bad path %s\n", bad[i]);
        CHECK(e == CONFIG_ERR_LAUNCH);
    }
}

TEST(path_length)
{
    char long_path[300];
    memset(long_path, 'a', sizeof long_path);
    long_path[259] = 0;
    CHECK(path_is_safe_relative(long_path));
    long_path[259] = 'a';
    long_path[260] = 0;
    CHECK(!path_is_safe_relative(long_path));
}

TEST(split_blobs_url)
{
    char host[64], prefix[64];
    unsigned port = 0;
    CHECK(config_split_https_url("https://cdn.example.com:8443/game/blobs/", host, sizeof host, &port, prefix, sizeof prefix));
    CHECK_STR(host, "cdn.example.com");
    CHECK_EQ_U64(port, 8443);
    CHECK_STR(prefix, "/game/blobs");
    CHECK(config_split_https_url("https://cdn.example.com", host, sizeof host, &port, prefix, sizeof prefix));
    CHECK_EQ_U64(port, 443);
    CHECK_STR(prefix, "");
    CHECK(!config_split_https_url("https://cdn.example.com:0/", host, sizeof host, &port, prefix, sizeof prefix));
    CHECK(!config_split_https_url("https://cdn.example.com/a?b", host, sizeof host, &port, prefix, sizeof prefix));
    CHECK(!config_split_https_url("https://u@cdn.example.com/", host, sizeof host, &port, prefix, sizeof prefix));
    CHECK(!config_split_https_url("http://cdn.example.com/", host, sizeof host, &port, prefix, sizeof prefix));
}

int main(void)
{
    RUN(split_blobs_url);
    RUN(valid);
    RUN(optional_fields);
    RUN(bad_fields);
    RUN(launch_paths);
    RUN(path_length);
    return test_report();
}
