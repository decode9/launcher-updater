#include "testlib.h"
#include "../src/core/prefs.h"

static json_token tokens[64];

static int parse(const char *text, char *out)
{
    return prefs_parse_lang(text, strlen(text), tokens, 64, out);
}

TEST(lang_valid)
{
    CHECK(prefs_lang_valid("es"));
    CHECK(prefs_lang_valid("pt-BR"));
    CHECK(prefs_lang_valid("zh-Hant-TW"));
    CHECK(prefs_lang_valid("abcdefghijklmnopqrstuvwxyz01234")); /* 31 characters */
    CHECK(!prefs_lang_valid("abcdefghijklmnopqrstuvwxyz012345"));
    CHECK(!prefs_lang_valid(""));
    CHECK(!prefs_lang_valid(0));
    CHECK(!prefs_lang_valid("-es"));
    CHECK(!prefs_lang_valid("es-"));
    CHECK(!prefs_lang_valid("1es"));
    CHECK(!prefs_lang_valid("es_ES"));
    CHECK(!prefs_lang_valid("es\""));
    CHECK(!prefs_lang_valid("../x"));
}

TEST(parse_lang)
{
    char lang[PREFS_LANG_BYTES];
    CHECK(parse("{\"lang\": \"es\"}", lang));
    CHECK_STR(lang, "es");
    CHECK(parse("{\"other\": [1, 2], \"lang\": \"pt-BR\", \"x\": null}", lang));
    CHECK_STR(lang, "pt-BR");
}

TEST(parse_tolerant)
{
    char lang[PREFS_LANG_BYTES];
    CHECK(!parse("", lang));
    CHECK_STR(lang, "");
    CHECK(!parse("{\"lang\": \"es\"", lang));
    CHECK_STR(lang, "");
    CHECK(!parse("[\"es\"]", lang));
    CHECK(!parse("{}", lang));
    CHECK(!parse("{\"lang\": 5}", lang));
    CHECK(!parse("{\"lang\": \"\"}", lang));
    CHECK_STR(lang, "");
    CHECK(!parse("{\"lang\": \"e s\"}", lang));
    CHECK_STR(lang, "");
    CHECK(!parse("{\"lang\": \"abcdefghijklmnopqrstuvwxyz0123456789\"}", lang));
    CHECK_STR(lang, "");
}

TEST(format)
{
    char doc[PREFS_DOC_BYTES], lang[PREFS_LANG_BYTES];
    size_t n = prefs_format(doc, sizeof doc, "pt-BR");
    CHECK_EQ_U64(n, strlen(doc));
    CHECK_STR(doc, "{\"lang\": \"pt-BR\"}\n");
    CHECK(prefs_parse_lang(doc, n, tokens, 64, lang));
    CHECK_STR(lang, "pt-BR");
    CHECK(prefs_format(doc, sizeof doc, "abcdefghijklmnopqrstuvwxyz01234") > 0);
    CHECK_STR(doc, "{\"lang\": \"abcdefghijklmnopqrstuvwxyz01234\"}\n");
    CHECK_EQ_U64(prefs_format(doc, sizeof doc, ""), 3);
    CHECK_STR(doc, "{}\n");
    CHECK_EQ_U64(prefs_format(doc, sizeof doc, "e\"s"), 0);
    CHECK_EQ_U64(prefs_format(doc, sizeof doc, 0), 0);
    CHECK_EQ_U64(prefs_format(doc, 8, "es"), 0);
}

int main(void)
{
    RUN(lang_valid);
    RUN(parse_lang);
    RUN(parse_tolerant);
    RUN(format);
    return test_report();
}
