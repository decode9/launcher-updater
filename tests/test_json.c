#include "testlib.h"
#include "../src/core/json.h"

static json_token tokens[256];

static json_error parse(json_doc *doc, const char *text)
{
    return json_parse(doc, text, strlen(text), tokens, 256);
}

TEST(objects_and_arrays)
{
    json_doc d;
    char s[64];
    uint64_t n;
    int b, arr;
    const char *text = " { \"a\": 1, \"b\": [true, false, null, \"x\"], \"c\": {\"d\": {}}, \"e\": [] } ";
    CHECK(parse(&d, text) == JSON_OK);
    CHECK(json_kind(&d, 0) == JSON_OBJECT);
    CHECK_EQ_U64(json_count(&d, 0), 4);
    CHECK(json_u64(&d, json_get(&d, 0, "a"), &n) && n == 1);
    arr = json_get(&d, 0, "b");
    CHECK_EQ_U64(json_count(&d, arr), 4);
    CHECK(json_bool(&d, json_at(&d, arr, 0), &b) && b == 1);
    CHECK(json_bool(&d, json_at(&d, arr, 1), &b) && b == 0);
    CHECK(json_is_null(&d, json_at(&d, arr, 2)));
    CHECK(json_string(&d, json_at(&d, arr, 3), s, sizeof s, 0));
    CHECK_STR(s, "x");
    CHECK(json_at(&d, arr, 4) == -1);
    CHECK(json_kind(&d, json_get(&d, json_get(&d, 0, "c"), "d")) == JSON_OBJECT);
    CHECK_EQ_U64(json_count(&d, json_get(&d, 0, "e")), 0);
    CHECK(json_first(&d, json_get(&d, 0, "e")) == -1);
    CHECK(json_get(&d, 0, "missing") == -1);
    CHECK(json_get(&d, json_get(&d, 0, "missing"), "x") == -1); /* chains through -1 */
    CHECK(json_get(&d, arr, "a") == -1);                       /* not an object */
}

TEST(iteration_skips_subtrees)
{
    json_doc d;
    uint64_t n, sum = 0;
    int e;
    CHECK(parse(&d, "[1, [2, [3]], {\"k\": [4]}, 5]") == JSON_OK);
    for (e = json_first(&d, 0); e >= 0; e = json_next(&d, e))
        if (json_u64(&d, e, &n)) sum += n;
    CHECK_EQ_U64(sum, 6);
    CHECK(json_next(&d, 0) == -1);
}

TEST(numbers)
{
    json_doc d;
    uint64_t n;
    CHECK(parse(&d, "[1600000000, 18446744073709551615, 18446744073709551616, -1, 1.5, 1e3, 0]") == JSON_OK);
    CHECK(json_u64(&d, json_at(&d, 0, 0), &n) && n == 1600000000ULL);
    CHECK(json_u64(&d, json_at(&d, 0, 1), &n) && n == 18446744073709551615ULL);
    CHECK(!json_u64(&d, json_at(&d, 0, 2), &n)); /* overflow */
    CHECK(!json_u64(&d, json_at(&d, 0, 3), &n)); /* negative */
    CHECK(!json_u64(&d, json_at(&d, 0, 4), &n)); /* fraction */
    CHECK(!json_u64(&d, json_at(&d, 0, 5), &n)); /* exponent */
    CHECK(json_u64(&d, json_at(&d, 0, 6), &n) && n == 0);
    CHECK(json_kind(&d, json_at(&d, 0, 4)) == JSON_NUMBER);
}

TEST(escapes)
{
    json_doc d;
    char s[64];
    size_t n;
    CHECK(parse(&d, "{\"k\\u0065y\": \"a\\\"b\\\\c\\/d\\n\\t\", \"u\": \"\\u00e9\\u20ac\\ud83d\\ude00\"}") == JSON_OK);
    CHECK(json_get(&d, 0, "key") >= 0); /* key compared after decoding */
    CHECK(json_string(&d, json_get(&d, 0, "key"), s, sizeof s, &n));
    CHECK_STR(s, "a\"b\\c/d\n\t");
    CHECK(json_string(&d, json_get(&d, 0, "u"), s, sizeof s, &n));
    CHECK_STR(s, "\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80");
    CHECK_EQ_U64(n, 9);
    CHECK(json_string_eq(&d, json_get(&d, 0, "u"), "\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80"));
    CHECK(!json_string_eq(&d, json_get(&d, 0, "u"), "\xc3\xa9"));
    CHECK(!json_string(&d, json_get(&d, 0, "u"), s, 9, 0)); /* needs 10 bytes with NUL */
}

TEST(raw_utf8_passes_through)
{
    json_doc d;
    char s[16];
    CHECK(parse(&d, "\"\xc3\xb1" "a\"") == JSON_OK);
    CHECK(json_string(&d, 0, s, sizeof s, 0));
    CHECK_STR(s, "\xc3\xb1" "a");
}

TEST(nul_escape_rejected_on_decode)
{
    json_doc d;
    char s[16];
    CHECK(parse(&d, "\"a\\u0000b\"") == JSON_OK);
    CHECK(!json_string(&d, 0, s, sizeof s, 0));
}

TEST(errors)
{
    static const char *const bad[] = {
        "", " ", "{", "}", "[1,]", "[1 2]", "{\"a\" 1}", "{\"a\":}", "{a:1}", "{\"a\":1,}",
        "tru", "nul", "01", "1.", "-", "1e", "+1", ".5", "\"abc", "\"a\nb\"", "\"\\x\"",
        "\"\\u12\"", "\"\\ud83d\"", "\"\\ude00\"", "\"\\ud83d\\u0041\"", "[1] 2", "{} {}", "'a'",
        "[\"a\"]]", "{\"a\":1]", "[1}",
    };
    json_doc d;
    size_t i;
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        json_error e = parse(&d, bad[i]);
        if (e != JSON_ERR_SYNTAX) fprintf(stderr, "accepted bad input #%zu: %s\n", i, bad[i]);
        CHECK(e == JSON_ERR_SYNTAX);
    }
}

TEST(limits)
{
    json_doc d;
    json_token few[3];
    char deep[200];
    int i;
    CHECK(json_parse(&d, "[1,2,3]", 7, few, 3) == JSON_ERR_NOMEM);
    CHECK(json_parse(&d, "[1,2]", 5, few, 3) == JSON_OK);
    for (i = 0; i < 100; i++) deep[i] = '[';
    for (i = 0; i < 100; i++) deep[100 + i] = ']';
    CHECK(json_parse(&d, deep, 200, tokens, 256) == JSON_ERR_DEPTH);
    CHECK(json_parse(&d, deep + 100 - JSON_MAX_DEPTH, 2 * JSON_MAX_DEPTH, tokens, 256) == JSON_OK);
}

TEST(scalar_root)
{
    json_doc d;
    uint64_t n;
    CHECK(parse(&d, " 42 ") == JSON_OK);
    CHECK(json_u64(&d, 0, &n) && n == 42);
    CHECK(json_next(&d, 0) == -1);
}

int main(void)
{
    RUN(objects_and_arrays);
    RUN(iteration_skips_subtrees);
    RUN(numbers);
    RUN(escapes);
    RUN(raw_utf8_passes_through);
    RUN(nul_escape_rejected_on_decode);
    RUN(errors);
    RUN(limits);
    RUN(scalar_root);
    return test_report();
}
