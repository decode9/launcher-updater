#include <stdlib.h>

#include "testlib.h"
#include "../src/core/str.h"
#include "../src/core/theme.h"

#define H1 "1111111111111111111111111111111111111111111111111111111111111111"
#define H2 "2222222222222222222222222222222222222222222222222222222222222222"
#define H3 "3333333333333333333333333333333333333333333333333333333333333333"

/* The example of docs/spec.md, "Theme". */
static const char spec_example[] =
    "{\n"
    "  \"schema_version\": 1,\n"
    "  \"min_launcher_version\": \"0.1.0\",\n"
    "  \"window\": {\"width\": 960, \"height\": 600, \"background\": {\"image\": \"" H1 "\", \"color\": \"bg\"}, \"radius\": 12, \"draggable\": true, \"title\": {\"en\": \"Example\", \"es\": \"Ejemplo\"}},\n"
    "  \"tokens\": {\"bg\": \"#101418FF\", \"primary\": \"#C89B3CFF\", \"text\": \"#F0E6D2FF\", \"muted\": \"#8A8A8AFF\", \"danger\": \"#D04040FF\"},\n"
    "  \"fonts\":  {\"body\": {\"family\": \"Segoe UI\", \"size\": 14, \"weight\": 400}, \"title\": {\"file\": \"" H2 "\", \"family\": \"Display\", \"size\": 28, \"weight\": 700}},\n"
    "  \"strings\": {\"en\": {\"play\": \"Play\", \"countdown\": \"Opens in {days}d {hours}:{minutes}:{seconds}\"}, \"es\": {\"play\": \"Jugar\"}},\n"
    "  \"elements\": [\n"
    "    {\"type\": \"image\", \"rect\": [40, 30, 300, 100], \"image\": \"" H3 "\"},\n"
    "    {\"type\": \"news\", \"rect\": [40, 150, 560, 300], \"interval\": 8},\n"
    "    {\"type\": \"countdown\", \"rect\": [620, 420, 300, 30], \"font\": \"title\", \"color\": \"primary\", \"text\": \"countdown\"},\n"
    "    {\"type\": \"status\", \"rect\": [40, 470, 560, 20], \"font\": \"body\", \"color\": \"muted\"},\n"
    "    {\"type\": \"progress\", \"rect\": [40, 495, 560, 14], \"style\": {\"normal\": {\"fill\": \"primary\", \"track\": \"#00000080\", \"radius\": 7}}},\n"
    "    {\"type\": \"button\", \"rect\": [760, 520, 160, 48], \"action\": \"play\", \"text\": \"play\",\n"
    "     \"style\": {\"normal\": {\"fill\": \"primary\", \"color\": \"bg\", \"radius\": 8}, \"hover\": {\"fill\": \"#E0B04CFF\"}, \"pressed\": {}, \"disabled\": {\"fill\": \"muted\"}}},\n"
    "    {\"type\": \"button\", \"rect\": [920, 8, 32, 32], \"action\": \"exit\", \"text\": \"\\u00d7\"}\n"
    "  ]\n"
    "}\n";

static json_token tokens[THEME_JSON_TOKENS];
static theme_model th;

static theme_error parse(const char *text)
{
    return theme_parse(&th, text, strlen(text), tokens, THEME_JSON_TOKENS);
}

static const theme_element *el(uint32_t i)
{
    const theme_element *e = theme_element_at(&th, i);
    if (!e) {
        fprintf(stderr, "missing element %u\n", i);
        exit(1);
    }
    return e;
}

TEST(contract_example)
{
    const theme_element *e;
    const theme_style *s;
    CHECK(parse(spec_example) == THEME_OK);
    CHECK_EQ_U64(th.warnings, 0);
    CHECK_EQ_U64(th.schema_version, 1);
    CHECK_EQ_U64(th.width, 960);
    CHECK_EQ_U64(th.height, 600);
    CHECK_EQ_U64(th.radius, 12);
    CHECK_EQ_U64(th.draggable, 1);
    CHECK_EQ_U64(th.background, 0x101418FF);
    CHECK(th.background_image == 1); /* blob 0 is the font file: fonts are read first */
    CHECK_STR(theme_title(&th, "es"), "Ejemplo");
    CHECK_STR(theme_title(&th, "fr"), "Example");

    CHECK_EQ_U64(th.font_count, 2);
    CHECK_STR(theme_str(&th, theme_font_at(&th, 0)->family), "Segoe UI");
    CHECK_EQ_U64(theme_font_at(&th, 1)->size, 28);
    CHECK_EQ_U64(theme_font_at(&th, 1)->weight, 700);
    CHECK(theme_font_at(&th, 1)->file >= 0);
    CHECK(theme_font_at(&th, 0)->file == -1);
    CHECK(th.default_font == 0);
    CHECK(theme_font_at(&th, -1) == 0);

    CHECK_EQ_U64(theme_element_count(&th), 7);
    e = el(0);
    CHECK_EQ_U64(e->type, THEME_EL_IMAGE);
    CHECK(e->rect.x == 40 && e->rect.y == 30 && e->rect.w == 300 && e->rect.h == 100);
    CHECK(e->image >= 0);
    CHECK_EQ_U64(el(1)->type, THEME_EL_NEWS);
    CHECK_EQ_U64(el(1)->interval, 8);

    e = el(2);
    CHECK_EQ_U64(e->type, THEME_EL_COUNTDOWN);
    s = theme_element_style(e, THEME_ST_NORMAL);
    CHECK_EQ_U64(s->color, 0xC89B3CFF);
    CHECK(s->font == 1);
    CHECK_STR(theme_text(&th, "es", theme_str(&th, e->text)), "Opens in {days}d {hours}:{minutes}:{seconds}");

    e = el(3);
    CHECK_EQ_U64(e->type, THEME_EL_STATUS);
    CHECK_EQ_U64(theme_element_style(e, THEME_ST_NORMAL)->color, 0x8A8A8AFF);

    e = el(4);
    CHECK_EQ_U64(e->type, THEME_EL_PROGRESS);
    s = theme_element_style(e, THEME_ST_NORMAL);
    CHECK_EQ_U64(s->fill, 0xC89B3CFF);
    CHECK_EQ_U64(s->track, 0x00000080);
    CHECK_EQ_U64(s->radius, 7);
    CHECK(s->font == 0); /* default font "body" */
    CHECK_EQ_U64(s->color, 0xF0E6D2FF); /* "text" token */

    e = el(5);
    CHECK_EQ_U64(e->type, THEME_EL_BUTTON);
    CHECK_EQ_U64(e->action, THEME_ACT_PLAY);
    CHECK_EQ_U64(e->align, THEME_ALIGN_CENTER);
    CHECK_STR(theme_text(&th, "es", theme_str(&th, e->text)), "Jugar");
    CHECK_EQ_U64(el(6)->action, THEME_ACT_EXIT);
    CHECK_STR(theme_text(&th, "es", theme_str(&th, el(6)->text)), "\xc3\x97");
    CHECK(theme_element_visible(el(6), THEME_LS_CHECKING) && theme_element_visible(el(6), THEME_LS_ERROR));

    CHECK(!theme_requires_update(&th, "0.1.0"));
    CHECK(!theme_requires_update(&th, "1.0.0"));
}

TEST(state_inheritance)
{
    const theme_element *e;
    const theme_style *n, *h, *p, *d;
    CHECK(parse(spec_example) == THEME_OK);
    e = el(5);
    n = theme_element_style(e, THEME_ST_NORMAL);
    h = theme_element_style(e, THEME_ST_HOVER);
    p = theme_element_style(e, THEME_ST_PRESSED);
    d = theme_element_style(e, THEME_ST_DISABLED);
    CHECK_EQ_U64(n->fill, 0xC89B3CFF);
    CHECK_EQ_U64(n->color, 0x101418FF);
    CHECK_EQ_U64(n->radius, 8);
    CHECK_EQ_U64(h->fill, 0xE0B04CFF); /* overridden */
    CHECK_EQ_U64(h->color, 0x101418FF); /* inherited */
    CHECK_EQ_U64(h->radius, 8);
    CHECK(memcmp(p, n, sizeof *n) == 0); /* "pressed": {} = normal */
    CHECK_EQ_U64(d->fill, 0x8A8A8AFF);
    CHECK_EQ_U64(d->radius, 8);
    CHECK(theme_element_style(e, (theme_state)99) == n);

    /* No style at all: every state is the element's base. */
    e = el(6);
    CHECK(memcmp(theme_element_style(e, THEME_ST_HOVER), theme_element_style(e, THEME_ST_NORMAL), sizeof(theme_style)) == 0);
    CHECK_EQ_U64(theme_element_style(e, THEME_ST_NORMAL)->fill, THEME_COLOR_NONE);

    /* Element color/font are the base, normal overrides, states override normal; an invalid
     * override keeps the inherited value. */
    CHECK(parse("{\"schema_version\":1,\"tokens\":{\"a\":\"#010203\"},"
                "\"fonts\":{\"body\":{\"family\":\"X\"},\"big\":{\"family\":\"Y\",\"size\":30}},"
                "\"elements\":[{\"type\":\"button\",\"rect\":[0,0,10,10],\"action\":\"exit\","
                "\"color\":\"a\",\"font\":\"big\",\"style\":{\"normal\":{\"border\":\"#FFFFFF\",\"border_width\":2},"
                "\"hover\":{\"color\":\"nope\",\"font\":\"body\",\"image\":\"" H1 "\"},\"pressed\":{\"color\":\"#00FF00\"}}}]}") == THEME_OK);
    e = el(0);
    n = theme_element_style(e, THEME_ST_NORMAL);
    h = theme_element_style(e, THEME_ST_HOVER);
    p = theme_element_style(e, THEME_ST_PRESSED);
    CHECK_EQ_U64(n->color, 0x010203FF);
    CHECK(n->font == 1);
    CHECK_EQ_U64(n->border, 0xFFFFFFFF);
    CHECK_EQ_U64(n->border_width, 2);
    CHECK_EQ_U64(h->color, 0x010203FF);
    CHECK(h->font == 0);
    CHECK(h->image == 0 && n->image == -1);
    CHECK_EQ_U64(p->color, 0x00FF00FF);
    CHECK_EQ_U64(p->border_width, 2);
    CHECK(th.warnings == THEME_WARN_BAD_COLOR);
}

TEST(colors)
{
    theme_color c;
    CHECK(parse("{\"schema_version\":1,\"tokens\":{\"accent\":\"#3C8BC8\",\"glass\":\"#FFFFFF80\","
                "\"bad\":\"#12\",\"ref\":\"accent\"}}") == THEME_OK);
    CHECK_EQ_U64(th.token_count, 2);
    CHECK(th.warnings & THEME_WARN_BAD_COLOR);
    CHECK(theme_color_resolve(&th, "accent", 7, &c) && c == 0x3C8BC8FF);
    CHECK(theme_color_resolve(&th, "glass", 7, &c) && c == 0xFFFFFF80);
    CHECK(theme_color_resolve(&th, "#a0B0c0", 7, &c) && c == 0xA0B0C0FF);
    CHECK(theme_color_resolve(&th, "#A0B0C011", 7, &c) && c == 0xA0B0C011);
    CHECK(!theme_color_resolve(&th, "#A0B0C", 7, &c) && c == 7);
    CHECK(!theme_color_resolve(&th, "#GGGGGG", 7, &c) && c == 7);
    CHECK(!theme_color_resolve(&th, "missing", 7, &c) && c == 7);
    CHECK(!theme_color_resolve(&th, "ref", 7, &c) && c == 7); /* tokens are literals only */
    CHECK(!theme_color_resolve(&th, "", 7, &c) && c == 7);
    CHECK(!theme_color_resolve(&th, 0, 7, &c) && c == 7);
    /* Window background defaults to the "bg" token, else a built-in colour. */
    CHECK_EQ_U64(th.background, THEME_COLOR_BG);
    CHECK(parse("{\"schema_version\":1,\"tokens\":{\"bg\":\"#000000\"},\"window\":{\"background\":{\"color\":12}}}") == THEME_OK);
    CHECK_EQ_U64(th.background, 0x000000FF);
    CHECK(th.warnings == THEME_WARN_BAD_COLOR);
    /* An invalid element colour keeps the default text colour. */
    CHECK(parse("{\"schema_version\":1,\"elements\":[{\"type\":\"text\",\"rect\":[0,0,5,5],\"color\":\"#zz\"}]}") == THEME_OK);
    CHECK_EQ_U64(theme_element_style(el(0), THEME_ST_NORMAL)->color, THEME_COLOR_TEXT);
}

TEST(languages)
{
    CHECK(parse("{\"schema_version\":1,\"window\":{\"title\":{\"es\":\"Juego\"}},"
                "\"strings\":{\"en\":{\"play\":\"Play\",\"exit\":\"Exit\",\"only_en\":\"E\"},"
                "\"es\":{\"play\":\"Jugar\",\"exit\":\"Salir\"},\"pt-BR\":{\"play\":\"Jogar\"},"
                "\"fr\":{\"play\":5}}}") == THEME_OK);
    CHECK_STR(theme_text(&th, "es", "play"), "Jugar");
    CHECK_STR(theme_text(&th, "ES", "play"), "Jugar");
    CHECK_STR(theme_text(&th, "es-MX", "exit"), "Salir"); /* base language */
    CHECK_STR(theme_text(&th, "es_VE", "exit"), "Salir");
    CHECK_STR(theme_text(&th, "es", "only_en"), "E");     /* falls back to en */
    CHECK_STR(theme_text(&th, "pt-BR", "play"), "Jogar");
    CHECK_STR(theme_text(&th, "pt-BR", "exit"), "Exit");
    CHECK_STR(theme_text(&th, "fr", "play"), "Play");     /* non-string value ignored */
    CHECK_STR(theme_text(&th, "de", "play"), "Play");
    CHECK_STR(theme_text(&th, 0, "play"), "Play");
    CHECK_STR(theme_text(&th, "es", "no_such_key"), "no_such_key");
    CHECK_STR(theme_title(&th, "es-AR"), "Juego");
    CHECK_STR(theme_title(&th, "en"), "");
    CHECK(th.warnings == THEME_WARN_BAD_VALUE);
    /* A plain-string title is the English (= default) title. */
    CHECK(parse("{\"schema_version\":1,\"window\":{\"title\":\"Game\"}}") == THEME_OK);
    CHECK_STR(theme_title(&th, "es"), "Game");
}

TEST(format)
{
    char out[128];
    theme_vars v = {0};
    v.days = "3", v.hours = "04", v.minutes = "05", v.seconds = "06";
    v.percent = "42", v.version = "1.2", v.done = "10 MB", v.total = "20 MB";
    CHECK(theme_format("Opens in {days}d {hours}:{minutes}:{seconds}", &v, out, sizeof out));
    CHECK_STR(out, "Opens in 3d 04:05:06");
    CHECK(theme_format("{percent}% {done}/{total} v{version}", &v, out, sizeof out));
    CHECK_STR(out, "42% 10 MB/20 MB v1.2");
    CHECK(theme_format("{unknown} {days {} {{days}} }{", &v, out, sizeof out));
    CHECK_STR(out, "{unknown} {days {} {3} }{");
    v.days = 0;
    CHECK(theme_format("{days}d", &v, out, sizeof out));
    CHECK_STR(out, "{days}d");
    CHECK(theme_format("{days}", 0, out, sizeof out));
    CHECK_STR(out, "{days}");
    CHECK(!theme_format("{percent}%%%%%%%%", &v, out, 6));
    CHECK_STR(out, "42%%%");
    CHECK(!theme_format("x", &v, out, 0));
}

TEST(unknown_type_and_fields)
{
    CHECK(parse("{\"schema_version\":1,\"future_section\":{\"a\":[1,2]},"
                "\"window\":{\"width\":800,\"height\":500,\"shadow\":true},"
                "\"elements\":[{\"type\":\"video\",\"rect\":[0,0,10,10]},"
                "{\"type\":\"text\",\"rect\":[1,2,3,4],\"text\":\"hi\",\"blink\":true,"
                "\"style\":{\"normal\":{\"gradient\":[1,2]},\"focused\":{\"fill\":\"#FFFFFF\"}}},"
                "{\"rect\":[0,0,1,1]},42,"
                "{\"type\":\"button\",\"rect\":[0,0,10,10],\"action\":\"settings\"},"
                "{\"type\":\"status\",\"rect\":[0,0,10,10],\"visible_when\":[\"error\",\"paused\"]}]}") == THEME_OK);
    CHECK_EQ_U64(th.width, 800);
    CHECK_EQ_U64(theme_element_count(&th), 2);
    CHECK_EQ_U64(th.skipped_elements, 4);
    CHECK(th.warnings & THEME_WARN_UNKNOWN_TYPE);
    CHECK(th.warnings & THEME_WARN_BAD_ELEMENT);
    CHECK_EQ_U64(el(0)->type, THEME_EL_TEXT);
    CHECK(el(0)->rect.x == 1 && el(0)->rect.h == 4);
    CHECK_EQ_U64(theme_element_style(el(0), THEME_ST_NORMAL)->fill, THEME_COLOR_NONE);
    CHECK(theme_element_visible(el(1), THEME_LS_ERROR));
    CHECK(!theme_element_visible(el(1), THEME_LS_READY));
    CHECK(!theme_element_visible(el(1), THEME_LS_COUNT));
}

TEST(buttons_and_rects)
{
    CHECK(parse("{\"schema_version\":1,\"window\":{\"width\":200,\"height\":100},\"elements\":["
                "{\"type\":\"button\",\"rect\":[0,0,10,10],\"action\":\"open_url\",\"url\":\"https://example.com/a?b=c\"},"
                "{\"type\":\"button\",\"rect\":[0,0,10,10],\"action\":\"open_url\",\"url\":\"http://example.com\"},"
                "{\"type\":\"button\",\"rect\":[0,0,10,10],\"action\":\"open_url\",\"url\":\"https://a b\"},"
                "{\"type\":\"button\",\"rect\":[0,0,10,10],\"action\":\"open_url\"},"
                "{\"type\":\"button\",\"rect\":[0,0,10,10],\"action\":\"minimize\",\"align\":\"right\"},"
                "{\"type\":\"text\",\"rect\":[150,80,100,100]},"
                "{\"type\":\"text\",\"rect\":[200,0,10,10]},"
                "{\"type\":\"text\",\"rect\":[-1,0,10,10]},"
                "{\"type\":\"text\",\"rect\":[0,0,0,10]},"
                "{\"type\":\"text\",\"rect\":[0,0,10]},"
                "{\"type\":\"image\",\"rect\":[0,0,10,10]},"
                "{\"type\":\"news\",\"rect\":[0,0,10,10],\"interval\":0}]}") == THEME_OK);
    CHECK_EQ_U64(theme_element_count(&th), 4);
    CHECK_EQ_U64(el(0)->action, THEME_ACT_OPEN_URL);
    CHECK_STR(theme_str(&th, el(0)->url), "https://example.com/a?b=c");
    CHECK_EQ_U64(el(1)->action, THEME_ACT_MINIMIZE);
    CHECK_EQ_U64(el(1)->align, THEME_ALIGN_RIGHT);
    CHECK(el(2)->rect.x == 150 && el(2)->rect.y == 80 && el(2)->rect.w == 50 && el(2)->rect.h == 20);
    CHECK_EQ_U64(el(3)->interval, 8);
    CHECK(th.warnings & THEME_WARN_CLIPPED);
    CHECK(th.warnings & THEME_WARN_BAD_VALUE);
    CHECK_EQ_U64(th.skipped_elements, 8);
}

TEST(versions_and_errors)
{
    CHECK(parse("{\"schema_version\":1,\"min_launcher_version\":\"0.3.0\"}") == THEME_OK);
    CHECK(theme_requires_update(&th, "0.1.0"));
    CHECK(theme_requires_update(&th, "0.2.9"));
    CHECK(!theme_requires_update(&th, "0.3.0"));
    CHECK(!theme_requires_update(&th, "0.10.0"));
    CHECK(!theme_requires_update(&th, "garbage"));
    CHECK(parse("{\"schema_version\":1}") == THEME_OK);
    CHECK(!theme_requires_update(&th, "0.0.0"));
    CHECK_EQ_U64(th.width, 960);
    CHECK_EQ_U64(th.height, 600);
    CHECK_EQ_U64(theme_element_count(&th), 0);

    CHECK(parse("{\"schema_version\":1,\"min_launcher_version\":\"1.0\"}") == THEME_ERR_VERSION);
    CHECK(parse("{\"schema_version\":1,\"min_launcher_version\":1}") == THEME_ERR_VERSION);
    CHECK(parse("{\"schema_version\":2}") == THEME_ERR_SCHEMA);
    CHECK(parse("{\"schema_version\":0}") == THEME_ERR_SCHEMA);
    CHECK(parse("{}") == THEME_ERR_SCHEMA);
    CHECK(parse("[1]") == THEME_ERR_ROOT);
    CHECK(parse("{\"schema_version\":1") == THEME_ERR_JSON);
    CHECK(theme_parse(&th, spec_example, strlen(spec_example), tokens, 10) == THEME_ERR_JSON);
}

static char big[200000];

TEST(limits)
{
    strbuf sb;
    int i, j;
    sb_init(&sb, big, sizeof big);
    sb_add(&sb, "{\"schema_version\":1,\"tokens\":{");
    for (i = 0; i < 40; i++) {
        sb_add(&sb, i ? ",\"t" : "\"t");
        sb_add_u64(&sb, (uint64_t)i);
        sb_add(&sb, "\":\"#102030\"");
    }
    sb_add(&sb, "},\"fonts\":{");
    for (i = 0; i < 10; i++) {
        sb_add(&sb, i ? ",\"f" : "\"f");
        sb_add_u64(&sb, (uint64_t)i);
        sb_add(&sb, "\":{\"family\":\"F\",\"file\":\"");
        for (j = 0; j < 64; j++) sb_add_char(&sb, "0123456789abcdef"[(i + j) & 15]);
        sb_add(&sb, "\"}");
    }
    sb_add(&sb, "},\"strings\":{");
    for (i = 0; i < 10; i++) {
        sb_add(&sb, i ? ",\"l" : "\"l");
        sb_add_u64(&sb, (uint64_t)i);
        sb_add(&sb, "\":{");
        for (j = 0; j < (i == 0 ? 130 : 1); j++) {
            sb_add(&sb, j ? ",\"k" : "\"k");
            sb_add_u64(&sb, (uint64_t)j);
            sb_add(&sb, "\":\"v\"");
        }
        sb_add(&sb, "}");
    }
    sb_add(&sb, "},\"elements\":[");
    for (i = 0; i < 70; i++) {
        sb_add(&sb, i ? ",{\"type\":\"image\",\"rect\":[0,0,1,1],\"image\":\"" : "{\"type\":\"image\",\"rect\":[0,0,1,1],\"image\":\"");
        for (j = 0; j < 62; j++) sb_add_char(&sb, 'a');
        sb_add_char(&sb, "0123456789abcdef"[i >> 4]);
        sb_add_char(&sb, "0123456789abcdef"[i & 15]);
        sb_add(&sb, "\"}");
    }
    sb_add(&sb, "]}");
    CHECK(sb.ok);
    CHECK(theme_parse(&th, big, sb.len, tokens, THEME_JSON_TOKENS) == THEME_OK);
    CHECK_EQ_U64(th.token_count, THEME_MAX_TOKENS);
    CHECK_EQ_U64(th.font_count, THEME_MAX_FONTS);
    CHECK_EQ_U64(th.lang_count, THEME_MAX_LANGS);
    CHECK_EQ_U64(th.langs[0].string_count, THEME_MAX_STRINGS);
    CHECK_STR(theme_text(&th, "l0", "k127"), "v");
    CHECK_STR(theme_text(&th, "l0", "k128"), "k128");
    CHECK_EQ_U64(th.blob_count, THEME_MAX_BLOBS);
    /* 8 font files fill 8 blob slots, 24 images fit, the remaining 40 images have no blob and
     * are skipped (an image element needs its image). */
    CHECK_EQ_U64(theme_element_count(&th), THEME_MAX_BLOBS - THEME_MAX_FONTS);
    CHECK_EQ_U64(th.skipped_elements, 70 - (THEME_MAX_BLOBS - THEME_MAX_FONTS));
    CHECK(th.warnings & THEME_WARN_LIMIT_TOKENS);
    CHECK(th.warnings & THEME_WARN_LIMIT_FONTS);
    CHECK(th.warnings & THEME_WARN_LIMIT_LANGS);
    CHECK(th.warnings & THEME_WARN_LIMIT_STRINGS);
    CHECK(th.warnings & THEME_WARN_LIMIT_BLOBS);

    /* More than THEME_MAX_ELEMENTS elements: the extra ones are dropped. */
    sb_init(&sb, big, sizeof big);
    sb_add(&sb, "{\"schema_version\":1,\"elements\":[");
    for (i = 0; i < 70; i++) sb_add(&sb, i ? ",{\"type\":\"text\",\"rect\":[0,0,1,1]}" : "{\"type\":\"text\",\"rect\":[0,0,1,1]}");
    sb_add(&sb, "]}");
    CHECK(theme_parse(&th, big, sb.len, tokens, THEME_JSON_TOKENS) == THEME_OK);
    CHECK_EQ_U64(theme_element_count(&th), THEME_MAX_ELEMENTS);
    CHECK_EQ_U64(th.skipped_elements, 6);
    CHECK(th.warnings == THEME_WARN_LIMIT_ELEMENTS);

    /* A full text pool drops strings, not the theme. */
    sb_init(&sb, big, sizeof big);
    sb_add(&sb, "{\"schema_version\":1,\"strings\":{\"en\":{");
    for (i = 0; i < 100; i++) {
        sb_add(&sb, i ? ",\"k" : "\"k");
        sb_add_u64(&sb, (uint64_t)i);
        sb_add(&sb, "\":\"");
        for (j = 0; j < 500; j++) sb_add_char(&sb, 'x');
        sb_add(&sb, "\"");
    }
    sb_add(&sb, "}}}");
    CHECK(theme_parse(&th, big, sb.len, tokens, THEME_JSON_TOKENS) == THEME_OK);
    CHECK(th.warnings & THEME_WARN_POOL_FULL);
    CHECK(th.pool_used <= THEME_POOL_BYTES);
    CHECK(str_len(theme_text(&th, "en", "k0")) == 500);
}

TEST(required_blobs)
{
    uint8_t out[THEME_MAX_BLOBS][SHA256_BYTES];
    uint8_t h1[SHA256_BYTES], h2[SHA256_BYTES], h3[SHA256_BYTES];
    CHECK(hex_decode(H1, 64, h1, SHA256_BYTES) && hex_decode(H2, 64, h2, SHA256_BYTES) &&
          hex_decode(H3, 64, h3, SHA256_BYTES));
    CHECK(parse(spec_example) == THEME_OK);
    CHECK_EQ_U64(theme_required_blobs(&th, 0, 0), 3);
    CHECK_EQ_U64(theme_required_blobs(&th, out, THEME_MAX_BLOBS), 3);
    CHECK(memcmp(out[0], h2, SHA256_BYTES) == 0); /* fonts are read before the window */
    CHECK(memcmp(out[1], h1, SHA256_BYTES) == 0);
    CHECK(memcmp(out[2], h3, SHA256_BYTES) == 0);
    CHECK(th.blobs[0].is_font && !th.blobs[1].is_font);
    CHECK_EQ_U64(theme_required_blobs(&th, out, 1), 3); /* count even when out is short */

    /* Duplicates are listed once; null and invalid hashes are not listed. */
    CHECK(parse("{\"schema_version\":1,\"window\":{\"background\":{\"image\":\"" H1 "\"}},"
                "\"elements\":[{\"type\":\"image\",\"rect\":[0,0,1,1],\"image\":\"" H1 "\"},"
                "{\"type\":\"button\",\"rect\":[0,0,1,1],\"action\":\"exit\",\"style\":{\"hover\":{\"image\":\"" H2 "\"},"
                "\"pressed\":{\"image\":\"" H2 "\"},\"disabled\":{\"image\":null}}},"
                "{\"type\":\"image\",\"rect\":[0,0,1,1],\"image\":\"ABC\"}]}") == THEME_OK);
    CHECK_EQ_U64(theme_required_blobs(&th, out, THEME_MAX_BLOBS), 2);
    CHECK(th.warnings & THEME_WARN_BAD_VALUE);
}

/* The shipped examples parse cleanly, without warnings, and use the expected element types. */
static void check_example_file(const char *rel, uint32_t want_types)
{
    char path[1024];
    static char text[65536];
    const char *here = __FILE__, *slash = strrchr(here, '/');
    FILE *f;
    size_t n;
    uint32_t i, types = 0;
    snprintf(path, sizeof path, "%.*s/../%s", slash ? (int)(slash - here) : 1, slash ? here : ".", rel);
    f = fopen(path, "rb");
    CHECK(f != 0);
    if (!f) return;
    n = fread(text, 1, sizeof text, f);
    fclose(f);
    CHECK(n < sizeof text);
    CHECK(theme_parse(&th, text, n, tokens, THEME_JSON_TOKENS) == THEME_OK);
    if (th.warnings || th.skipped_elements) fprintf(stderr, "%s: warnings 0x%x\n", rel, th.warnings);
    CHECK_EQ_U64(th.warnings, 0);
    CHECK_EQ_U64(th.skipped_elements, 0);
    CHECK(!theme_requires_update(&th, "0.1.0"));
    for (i = 0; i < theme_element_count(&th); i++) types |= 1u << el(i)->type;
    CHECK_EQ_U64(types, want_types);
    CHECK(theme_title(&th, "es")[0] && theme_title(&th, "en")[0]);
}

TEST(examples)
{
    const uint32_t all = (1u << THEME_EL_TYPE_COUNT) - 1;
    /* The neutral theme is colours only, so it has every type but image. */
    check_example_file("examples/theme-neutral/theme.json", all & ~(1u << THEME_EL_IMAGE));
    CHECK_EQ_U64(theme_required_blobs(&th, 0, 0), 0); /* the embedded theme needs no download */
    check_example_file("examples/theme-demo/theme.json", all);
    CHECK(theme_required_blobs(&th, 0, 0) > 0);
}

int main(void)
{
    RUN(contract_example);
    RUN(state_inheritance);
    RUN(colors);
    RUN(languages);
    RUN(format);
    RUN(unknown_type_and_fields);
    RUN(buttons_and_rects);
    RUN(versions_and_errors);
    RUN(limits);
    RUN(required_blobs);
    RUN(examples);
    return test_report();
}
