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

#define THEME_ONLY "\"theme\": {\"sha256\": \"" H2 "\", \"size\": 1}"

TEST(no_release)
{
    CHECK(parse("{\"schema\": 1, \"server_time\": \"2026-11-12T00:00:00Z\", \"open\": true, " THEME_ONLY "}", 0) ==
          STATUS_OK);
    CHECK(!st.release.present);
    CHECK(st.theme.present);
    CHECK(parse("{\"schema\": 1, \"server_time\": \"2026-11-12T00:00:00Z\", \"open\": true, " THEME_ONLY
                ", \"release\": null}", 0) == STATUS_OK);
    CHECK(!st.release.present);
    /* a release that is there must still be complete */
    CHECK(parse("{\"schema\": 1, \"server_time\": \"2026-11-12T00:00:00Z\", \"open\": true, " THEME_ONLY
                ", \"release\": {\"version\": \"\", \"sha256\": \"" H3 "\", \"size\": 2}}", 0) == STATUS_ERR_BLOB);
    CHECK(parse("{\"schema\": 1, \"server_time\": \"2026-11-12T00:00:00Z\", \"open\": true, " THEME_ONLY
                ", \"release\": {\"sha256\": \"" H3 "\", \"size\": 2}}", 0) == STATUS_ERR_BLOB);
    CHECK(parse("{\"schema\": 1, \"server_time\": \"2026-11-12T00:00:00Z\", \"open\": true, " THEME_ONLY
                ", \"release\": \"r1\"}", 0) == STATUS_ERR_BLOB);
    /* the release is reset by every parse */
    CHECK(parse("{\"schema\": 1, \"server_time\": \"2026-11-12T00:00:00Z\", \"open\": true, " BLOBS "}", 0) == STATUS_OK);
    CHECK(st.release.present);
    CHECK(parse("{\"schema\": 1, \"server_time\": \"2026-11-12T00:00:00Z\", \"open\": true, " THEME_ONLY "}", 0) ==
          STATUS_OK);
    CHECK(!st.release.present);
    CHECK_STR(st.release.version, ""); /* no stale version on screen */
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

TEST(news_category_and_date)
{
    CHECK(parse("{\"schema\": 1, \"server_time\": \"2026-11-12T00:00:00Z\", \"open\": true, " BLOBS ", \"news\": ["
                "{\"title\": \"a\", \"category\": \"updates\", \"date\": \"2026-10-28\"},"
                "{\"title\": \"b\", \"category\": \"patch_notes-2\", \"date\": \"2024-02-29\"},"
                "{\"title\": \"c\"},"
                "{\"title\": \"d\", \"category\": \"Updates\", \"date\": \"2026-02-29\"},"
                "{\"title\": \"e\", \"category\": \"\", \"date\": \"2026-10-28T00:00:00Z\"},"
                "{\"title\": \"f\", \"category\": \"has space\", \"date\": \"26-10-28\"},"
                "{\"title\": \"g\", \"category\": \"abcdefghijabcdefghijabcdefghij1\", \"date\": \"1969-12-31\"},"
                "{\"title\": \"h\", \"category\": \"abcdefghijabcdefghijabcdefghij12\", \"date\": \"2026-13-01\"},"
                "{\"title\": \"i\", \"category\": 7, \"date\": 20261028},"
                "{\"title\": \"j\", \"category\": null, \"date\": null},"
                "{\"title\": \"k\", \"category\": \"caf\u00e9\", \"date\": \"2026-1O-28\"}]}",
                0) == STATUS_OK);
    CHECK_EQ_U64(st.news_count, 11);
    CHECK_STR(st.news[0].category, "updates");
    CHECK_STR(st.news[0].date, "2026-10-28");
    CHECK_STR(st.news[1].category, "patch_notes-2");
    CHECK_STR(st.news[1].date, "2024-02-29");          /* leap day */
    CHECK_STR(st.news[2].category, "");                 /* absent */
    CHECK_STR(st.news[2].date, "");
    CHECK_STR(st.news[3].category, "");                 /* uppercase */
    CHECK_STR(st.news[3].date, "");                     /* not a leap year */
    CHECK_STR(st.news[4].category, "");
    CHECK_STR(st.news[4].date, "");                     /* a time, not a date */
    CHECK_STR(st.news[5].category, "");
    CHECK_STR(st.news[5].date, "");
    CHECK_STR(st.news[6].category, "abcdefghijabcdefghijabcdefghij1"); /* 31 bytes: the limit */
    CHECK_STR(st.news[6].date, "");                     /* before 1970 */
    CHECK_STR(st.news[7].category, "");                 /* 32 bytes: too long */
    CHECK_STR(st.news[7].date, "");
    CHECK_STR(st.news[8].category, "");                 /* wrong types */
    CHECK_STR(st.news[8].date, "");
    CHECK_STR(st.news[9].category, "");
    CHECK_STR(st.news[9].date, "");
    CHECK_STR(st.news[10].category, "");                /* non-ASCII */
    CHECK_STR(st.news[10].date, "");
    CHECK_STR(st.news[10].title, "k");                  /* the item itself still shows */
    /* reset by every parse */
    CHECK(parse("{\"schema\": 1, \"server_time\": \"2026-11-12T00:00:00Z\", \"open\": true, " BLOBS
                ", \"news\": [{\"title\": \"a\"}]}", 0) == STATUS_OK);
    CHECK_STR(st.news[0].category, "");
    CHECK_STR(st.news[0].date, "");
}

TEST(date_valid)
{
    CHECK(status_date_is_valid("2026-10-28", 10));
    CHECK(status_date_is_valid("1970-01-01", 10));
    CHECK(status_date_is_valid("2000-02-29", 10));
    CHECK(!status_date_is_valid("1900-02-29", 10));
    CHECK(!status_date_is_valid("2026-04-31", 10));
    CHECK(!status_date_is_valid("2026-00-10", 10));
    CHECK(!status_date_is_valid("2026-10-00", 10));
    CHECK(!status_date_is_valid("2026/10/28", 10));
    CHECK(!status_date_is_valid("2026-10-28", 9));
    CHECK(!status_date_is_valid("", 0));
}

static status_news news_item(const char *title, int image_byte)
{
    status_news n;
    memset(&n, 0, sizeof n);
    strcpy(n.title, title);
    if (image_byte) {
        n.has_image = 1;
        memset(n.image, image_byte, sizeof n.image);
    }
    return n;
}

TEST(news_equal)
{
    status_news a[2], b[2];
    a[0] = news_item("one", 1);
    a[1] = news_item("two", 0);
    memcpy(b, a, sizeof a);
    CHECK(status_news_equal(a, 2, b, 2));
    CHECK(status_news_equal(a, 0, b, 0));
    CHECK(!status_news_equal(a, 2, b, 1));
    b[0].image[5] = 9; /* another image */
    CHECK(!status_news_equal(a, 2, b, 2));
    b[0] = a[0];
    b[0].has_image = 0; /* image dropped */
    CHECK(!status_news_equal(a, 2, b, 2));
    b[0] = a[0];
    strcpy(b[1].link, "https://example.com/x");
    CHECK(!status_news_equal(a, 2, b, 2));
    b[1] = a[1];
    strcpy(b[1].title, "deux");
    CHECK(!status_news_equal(a, 2, b, 2));
    b[1] = a[1];
    strcpy(b[1].category, "events");
    CHECK(!status_news_equal(a, 2, b, 2));
    b[1] = a[1];
    strcpy(b[1].date, "2026-10-28");
    CHECK(!status_news_equal(a, 2, b, 2));
    b[1] = a[1];
    a[1].image[0] = 7; /* ignored without has_image */
    CHECK(status_news_equal(a, 2, b, 2));
}

TEST(news_keep_index)
{
    status_news old[3], cur[3];
    old[0] = news_item("a", 1);
    old[1] = news_item("b", 2);
    old[2] = news_item("c", 0);
    CHECK_EQ_U64(status_news_keep_index(old, 3, 1, old, 3), 1);      /* same list: same item */
    cur[0] = old[2];
    cur[1] = old[0];
    cur[2] = old[1];
    CHECK_EQ_U64(status_news_keep_index(old, 3, 1, cur, 3), 2);      /* item moved */
    cur[0] = news_item("x", 0);
    cur[1] = news_item("y", 0);
    CHECK_EQ_U64(status_news_keep_index(old, 3, 1, cur, 2), 1);      /* gone: index kept while it fits */
    CHECK_EQ_U64(status_news_keep_index(old, 3, 2, cur, 2), 0);      /* gone and out of range */
    CHECK_EQ_U64(status_news_keep_index(old, 3, 0, cur, 0), 0);      /* empty list */
    CHECK_EQ_U64(status_news_keep_index(old, 0, 0, cur, 2), 0);      /* nothing shown before */
    CHECK_EQ_U64(status_news_keep_index(old, 3, 5, old, 3), 0);      /* stale index */
}

int main(void)
{
    RUN(utc);
    RUN(spec_example_parses);
    RUN(language_fallback);
    RUN(minimal_and_open);
    RUN(no_release);
    RUN(news_details);
    RUN(news_category_and_date);
    RUN(date_valid);
    RUN(errors);
    RUN(news_equal);
    RUN(news_keep_index);
    return test_report();
}
