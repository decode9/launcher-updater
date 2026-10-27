#include "testlib.h"
#include "../src/core/status.h"
#include "../src/core/str.h"

#define H1 "1111111111111111111111111111111111111111111111111111111111111111"
#define H2 "2222222222222222222222222222222222222222222222222222222222222222"
#define H3 "3333333333333333333333333333333333333333333333333333333333333333"
#define H4 "4444444444444444444444444444444444444444444444444444444444444444"

/* The example of docs/spec.md, "/v1/status". */
static const char spec_example[] =
    "{\n"
    "  \"schema\": 1,\n"
    "  \"server_time\": \"2026-10-27T15:00:00Z\",\n"
    "  \"open\": false,\n"
    "  \"opens_at\": \"2026-11-11T13:00:00Z\",\n"
    "  \"news\": [\n"
    "    {\"image\": \"" H1 "\", \"title\": {\"en\": \"Beta starts 11.11.2026\", \"es\": \"La beta empieza el 11.11.2026\"}, \"link\": \"https://example.com/news/1\"}\n"
    "  ],\n"
    "  \"theme\":    {\"sha256\": \"" H2 "\", \"size\": 1234},\n"
    "  \"release\":  {\"version\": \"2026.10.27.1\", \"sha256\": \"" H3 "\", \"size\": 5678},\n"
    "  \"launcher\": {\"version\": \"0.1.0\", \"sha256\": \"" H4 "\", \"size\": 300000}\n"
    "}\n";

static json_token tokens[512];
static launcher_status st;

static status_error parse(const char *text, const char *lang)
{
    return status_parse(&st, text, strlen(text), tokens, 512, lang);
}

TEST(utc)
{
    int64_t t;
    CHECK(utc_parse("1970-01-01T00:00:00Z", 20, &t) && t == 0);
    CHECK(utc_parse("2026-10-27T15:00:00Z", 20, &t) && t == 1793113200LL);
    CHECK(utc_parse("2000-02-29T23:59:59Z", 20, &t) && t == 951868799LL);
    CHECK(utc_parse("2026-10-27T15:00:00.123Z", 24, &t) && t == 1793113200LL);
    CHECK(!utc_parse("2026-02-29T00:00:00Z", 20, &t));
    CHECK(!utc_parse("2026-10-27 15:00:00Z", 20, &t));
    CHECK(!utc_parse("2026-10-27T15:00:00", 19, &t));
    CHECK(!utc_parse("2026-10-27T15:00:00+01:00", 25, &t));
    CHECK(!utc_parse("2026-13-01T00:00:00Z", 20, &t));
    CHECK(!utc_parse("1969-12-31T23:59:59Z", 20, &t));
    CHECK(!utc_parse("2026-10-27T24:00:00Z", 20, &t));
}

TEST(spec_example_parses)
{
    char hex[65];
    CHECK(parse(spec_example, "es") == STATUS_OK);
    CHECK_EQ_U64(st.schema, 1);
    CHECK(!st.open);
    CHECK(st.has_opens_at);
    CHECK_EQ_U64(status_seconds_to_open(&st), 15 * 86400 - 2 * 3600);
    CHECK_EQ_U64(st.news_count, 1);
    CHECK(st.news[0].has_image);
    CHECK_STR(st.news[0].title, "La beta empieza el 11.11.2026");
    CHECK_STR(st.news[0].link, "https://example.com/news/1");
    CHECK(st.theme.present && st.theme.size == 1234);
    hex_encode(st.theme.sha256, 32, hex);
    CHECK_STR(hex, H2);
    CHECK_STR(st.release.version, "2026.10.27.1");
    CHECK_EQ_U64(st.release.size, 5678);
    CHECK(st.launcher.present);
    CHECK_STR(st.launcher.version, "0.1.0");
    CHECK_EQ_U64(st.launcher.size, 300000);
}

TEST(language_fallback)
{
    CHECK(parse(spec_example, "fr") == STATUS_OK);
    CHECK_STR(st.news[0].title, "Beta starts 11.11.2026");
    CHECK(parse(spec_example, 0) == STATUS_OK);
    CHECK_STR(st.news[0].title, "Beta starts 11.11.2026");
}

#define BLOBS "\"theme\": {\"sha256\": \"" H2 "\", \"size\": 1}, \"release\": {\"version\": \"r1\", \"sha256\": \"" H3 "\", \"size\": 2}"

TEST(minimal_and_open)
{
    CHECK(parse("{\"schema\": 1, \"server_time\": \"2026-11-12T00:00:00Z\", \"open\": true, \"opens_at\": null, "
                BLOBS ", \"extra\": {\"x\": [1]}}", "en") == STATUS_OK);
    CHECK(st.open);
    CHECK(!st.has_opens_at);
    CHECK(status_seconds_to_open(&st) == -1);
    CHECK(!st.launcher.present);
    CHECK_EQ_U64(st.news_count, 0);
    CHECK(parse("{\"schema\": 1, \"server_time\": \"2026-11-12T00:00:00Z\", \"open\": true, "
                "\"opens_at\": \"2026-11-11T13:00:00Z\", " BLOBS "}", "en") == STATUS_OK);
    CHECK(status_seconds_to_open(&st) == 0);
}

TEST(news_details)
{
    CHECK(parse("{\"schema\": 1, \"server_time\": \"2026-11-12T00:00:00Z\", \"open\": true, " BLOBS ", \"news\": ["
                "{\"title\": {\"de\": \"Nur\"}, \"link\": \"http://insecure\"}, {\"title\": \"plain\", \"image\": null}]}",
                "es") == STATUS_OK);
    CHECK_EQ_U64(st.news_count, 2);
    CHECK_STR(st.news[0].title, "Nur");   /* first language when neither lang nor en exist */
    CHECK_STR(st.news[0].link, "");       /* non-https link dropped */
    CHECK(!st.news[0].has_image);
    CHECK_STR(st.news[1].title, "plain");
}

TEST(errors)
{
    CHECK(parse("{\"schema\": 2, \"server_time\": \"2026-11-12T00:00:00Z\", \"open\": true, " BLOBS "}", 0) == STATUS_ERR_SCHEMA);
    CHECK(parse("{\"server_time\": \"2026-11-12T00:00:00Z\", \"open\": true, " BLOBS "}", 0) == STATUS_ERR_SCHEMA);
    CHECK(parse("{\"schema\": 1, \"server_time\": \"yesterday\", \"open\": true, " BLOBS "}", 0) == STATUS_ERR_TIME);
    CHECK(parse("{\"schema\": 1, \"server_time\": \"2026-11-12T00:00:00Z\", \"open\": 1, " BLOBS "}", 0) == STATUS_ERR_OPEN);
    CHECK(parse("{\"schema\": 1, \"server_time\": \"2026-11-12T00:00:00Z\", \"open\": true}", 0) == STATUS_ERR_BLOB);
    CHECK(parse("{\"schema\": 1, \"server_time\": \"2026-11-12T00:00:00Z\", \"open\": true, " BLOBS
                ", \"launcher\": {\"version\": \"1.0\", \"sha256\": \"" H4 "\", \"size\": 1}}", 0) == STATUS_ERR_BLOB);
    CHECK(parse("{\"schema\": 1, \"server_time\": \"2026-11-12T00:00:00Z\", \"open\": true, "
                "\"theme\": {\"sha256\": \"abc\", \"size\": 1}, \"release\": {\"version\": \"r\", \"sha256\": \"" H3 "\", \"size\": 2}}", 0)
          == STATUS_ERR_BLOB);
    CHECK(parse("{\"schema\": 1, \"server_time\": \"2026-11-12T00:00:00Z\", \"open\": true, " BLOBS ", \"news\": {}}", 0) == STATUS_ERR_NEWS);
    CHECK(parse("[1]", 0) == STATUS_ERR_JSON);
}

int main(void)
{
    RUN(utc);
    RUN(spec_example_parses);
    RUN(language_fallback);
    RUN(minimal_and_open);
    RUN(news_details);
    RUN(errors);
    return test_report();
}
