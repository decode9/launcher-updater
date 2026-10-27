/*
 * theme.c - theme.json to theme_model (see theme.h for the rules).
 */
#include "theme.h"

#include "str.h"
#include "theme_internal.h"

#define COLOR_TEXT_BYTES 64 /* longest colour value we read: token names are shorter */
#define DEFAULT_TRACK 0xFFFFFF33u
#define MAX_RECT_COORD 65535u
#define MAX_RADIUS 2048u
#define MAX_BORDER 64u

/* ---------------------------------------------------------------- small helpers */

static void zero(void *p, size_t n)
{
    uint8_t *b = (uint8_t *)p;
    while (n--) *b++ = 0;
}

static char lower(char ch) { return (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch; }

/* ASCII case-insensitive equality of a and the first n bytes of b (b[n] must end b or be
 * ignored: the caller passes n = length of b's prefix to compare). */
static int lang_eq_n(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (!a[i] || lower(a[i]) != lower(b[i])) return 0;
    return a[n] == 0;
}

static int hex_digit(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    ch = lower(ch);
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

static int parse_hex_color(const char *s, theme_color *out)
{
    size_t n = str_len(s), i;
    uint32_t v = 0;
    if (s[0] != '#' || (n != 7 && n != 9)) return 0;
    for (i = 1; i < n; i++) {
        int d = hex_digit(s[i]);
        if (d < 0) return 0;
        v = (v << 4) | (uint32_t)d;
    }
    *out = n == 7 ? (v << 8) | 0xFFu : v;
    return 1;
}

/* Decodes a JSON string token into buf; 0 if missing, not a string or too long. */
static int read_name(const json_doc *doc, int tok, char *buf, size_t cap)
{
    return json_kind(doc, tok) == JSON_STRING && json_string(doc, tok, buf, cap, 0);
}

/* ---------------------------------------------------------------- context helpers */

theme_sref theme_ctx_string(theme_ctx *c, int tok)
{
    theme_model *t = c->theme;
    size_t n;
    if (json_kind(c->doc, tok) != JSON_STRING) return 0;
    if (!json_string(c->doc, tok, t->pool + t->pool_used, THEME_POOL_BYTES - t->pool_used, &n)) {
        t->warnings |= THEME_WARN_POOL_FULL;
        return 0;
    }
    if (n == 0) return 0;
    {
        theme_sref ref = (theme_sref)t->pool_used;
        t->pool_used += (uint32_t)n + 1;
        return ref;
    }
}

int theme_ctx_uint(theme_ctx *c, int tok, uint32_t min, uint32_t max, uint32_t *out)
{
    uint64_t v;
    if (tok < 0) return 0;
    if (!json_u64(c->doc, tok, &v) || v < min || v > max) {
        c->theme->warnings |= THEME_WARN_BAD_VALUE;
        return 0;
    }
    *out = (uint32_t)v;
    return 1;
}

int theme_ctx_blob(theme_ctx *c, int tok, int is_font)
{
    theme_model *t = c->theme;
    char hex[2 * SHA256_BYTES + 1];
    uint8_t sha[SHA256_BYTES];
    size_t n;
    uint32_t i;
    if (tok < 0 || json_is_null(c->doc, tok)) return -1;
    if (json_kind(c->doc, tok) != JSON_STRING || !json_string(c->doc, tok, hex, sizeof hex, &n) ||
        !hex_decode(hex, n, sha, SHA256_BYTES)) {
        t->warnings |= THEME_WARN_BAD_VALUE;
        return -1;
    }
    for (i = 0; i < t->blob_count; i++)
        if (mem_eq(t->blobs[i].sha256, sha, SHA256_BYTES)) return (int)i;
    if (t->blob_count >= THEME_MAX_BLOBS) {
        t->warnings |= THEME_WARN_LIMIT_BLOBS;
        return -1;
    }
    for (n = 0; n < SHA256_BYTES; n++) t->blobs[t->blob_count].sha256[n] = sha[n];
    t->blobs[t->blob_count].is_font = (uint8_t)(is_font ? 1 : 0);
    return (int)t->blob_count++;
}

/* Colour field: missing leaves *out alone (returns 0); present but invalid also leaves it and
 * flags THEME_WARN_BAD_COLOR. */
static int ctx_color(theme_ctx *c, int tok, theme_color *out)
{
    char text[COLOR_TEXT_BYTES];
    if (tok < 0) return 0;
    if (!read_name(c->doc, tok, text, sizeof text) || !theme_color_resolve(c->theme, text, *out, out)) {
        c->theme->warnings |= THEME_WARN_BAD_COLOR;
        return 0;
    }
    return 1;
}

/* Font field: missing leaves *out alone; an unknown name flags THEME_WARN_BAD_VALUE. */
static void ctx_font(theme_ctx *c, int tok, int16_t *out)
{
    const theme_model *t = c->theme;
    uint16_t i;
    if (tok < 0) return;
    for (i = 0; i < t->font_count; i++) {
        if (json_string_eq(c->doc, tok, t->fonts[i].name)) {
            *out = (int16_t)i;
            return;
        }
    }
    c->theme->warnings |= THEME_WARN_BAD_VALUE;
}

/* ---------------------------------------------------------------- sections */

static theme_lang *find_or_add_lang(theme_model *t, const char *name)
{
    uint16_t i;
    for (i = 0; i < t->lang_count; i++)
        if (lang_eq_n(t->langs[i].name, name, str_len(name))) return &t->langs[i];
    if (t->lang_count >= THEME_MAX_LANGS) {
        t->warnings |= THEME_WARN_LIMIT_LANGS;
        return 0;
    }
    str_copy(t->langs[t->lang_count].name, THEME_NAME_BYTES, name);
    return &t->langs[t->lang_count++];
}

static void parse_tokens(theme_ctx *c, int object)
{
    theme_model *t = c->theme;
    int k;
    for (k = json_first(c->doc, object); k >= 0; k = json_next(c->doc, json_next(c->doc, k))) {
        theme_token *tok;
        char value[COLOR_TEXT_BYTES];
        if (t->token_count >= THEME_MAX_TOKENS) {
            t->warnings |= THEME_WARN_LIMIT_TOKENS;
            return;
        }
        tok = &t->tokens[t->token_count];
        if (!read_name(c->doc, k, tok->name, THEME_NAME_BYTES) || !tok->name[0] || tok->name[0] == '#') {
            t->warnings |= THEME_WARN_BAD_VALUE;
            continue;
        }
        /* Token values are literal colours: tokens never refer to other tokens. */
        if (!read_name(c->doc, json_next(c->doc, k), value, sizeof value) ||
            !parse_hex_color(value, &tok->value)) {
            t->warnings |= THEME_WARN_BAD_COLOR;
            continue;
        }
        t->token_count++;
    }
}

static void parse_fonts(theme_ctx *c, int object)
{
    theme_model *t = c->theme;
    int k;
    for (k = json_first(c->doc, object); k >= 0; k = json_next(c->doc, json_next(c->doc, k))) {
        int def = json_next(c->doc, k);
        theme_font *f;
        uint32_t v;
        if (t->font_count >= THEME_MAX_FONTS) {
            t->warnings |= THEME_WARN_LIMIT_FONTS;
            return;
        }
        f = &t->fonts[t->font_count];
        if (!read_name(c->doc, k, f->name, THEME_NAME_BYTES) || json_kind(c->doc, def) != JSON_OBJECT) {
            t->warnings |= THEME_WARN_BAD_VALUE;
            continue;
        }
        f->family = theme_ctx_string(c, json_get(c->doc, def, "family"));
        v = 14;
        theme_ctx_uint(c, json_get(c->doc, def, "size"), 6, 200, &v);
        f->size = (uint16_t)v;
        v = 400;
        theme_ctx_uint(c, json_get(c->doc, def, "weight"), 100, 900, &v);
        f->weight = (uint16_t)v;
        f->file = (int16_t)theme_ctx_blob(c, json_get(c->doc, def, "file"), 1);
        if (str_eq(f->name, "body")) t->default_font = (int16_t)t->font_count;
        t->font_count++;
    }
}

static void parse_strings(theme_ctx *c, int object)
{
    theme_model *t = c->theme;
    int k;
    for (k = json_first(c->doc, object); k >= 0; k = json_next(c->doc, json_next(c->doc, k))) {
        char name[THEME_NAME_BYTES];
        int table = json_next(c->doc, k), s;
        theme_lang *lang;
        if (!read_name(c->doc, k, name, sizeof name) || !name[0] || json_kind(c->doc, table) != JSON_OBJECT) {
            t->warnings |= THEME_WARN_BAD_VALUE;
            continue;
        }
        if (!(lang = find_or_add_lang(t, name))) return;
        for (s = json_first(c->doc, table); s >= 0; s = json_next(c->doc, json_next(c->doc, s))) {
            theme_string *str;
            if (lang->string_count >= THEME_MAX_STRINGS) {
                t->warnings |= THEME_WARN_LIMIT_STRINGS;
                break;
            }
            if (json_kind(c->doc, json_next(c->doc, s)) != JSON_STRING) {
                t->warnings |= THEME_WARN_BAD_VALUE;
                continue;
            }
            str = &lang->strings[lang->string_count];
            str->key = theme_ctx_string(c, s);
            str->value = theme_ctx_string(c, json_next(c->doc, s));
            if (str->key) lang->string_count++;
        }
    }
}

static void parse_window(theme_ctx *c, int object)
{
    theme_model *t = c->theme;
    int bg = json_get(c->doc, object, "background"), title = json_get(c->doc, object, "title");
    uint32_t v;
    int draggable;

    if (theme_ctx_uint(c, json_get(c->doc, object, "width"), 64, THEME_MAX_SIDE, &v)) t->width = (uint16_t)v;
    if (theme_ctx_uint(c, json_get(c->doc, object, "height"), 64, THEME_MAX_SIDE, &v)) t->height = (uint16_t)v;
    if (theme_ctx_uint(c, json_get(c->doc, object, "radius"), 0, MAX_RADIUS, &v)) t->radius = (uint16_t)v;
    if (json_bool(c->doc, json_get(c->doc, object, "draggable"), &draggable)) t->draggable = (uint8_t)draggable;

    if (json_kind(c->doc, bg) == JSON_STRING) {
        ctx_color(c, bg, &t->background);
    } else {
        ctx_color(c, json_get(c->doc, bg, "color"), &t->background);
        t->background_image = (int16_t)theme_ctx_blob(c, json_get(c->doc, bg, "image"), 0);
    }

    if (json_kind(c->doc, title) == JSON_STRING) {
        theme_lang *en = find_or_add_lang(t, "en");
        if (en) en->title = theme_ctx_string(c, title);
    } else {
        int k;
        for (k = json_first(c->doc, title); k >= 0; k = json_next(c->doc, json_next(c->doc, k))) {
            char name[THEME_NAME_BYTES];
            theme_lang *lang;
            if (!read_name(c->doc, k, name, sizeof name) || !name[0]) continue;
            if ((lang = find_or_add_lang(t, name))) lang->title = theme_ctx_string(c, json_next(c->doc, k));
        }
    }
}

/* ---------------------------------------------------------------- elements */

static const char *const LAUNCHER_STATES[THEME_LS_COUNT] = {"checking", "locked", "ready", "installing", "error"};
static const char *const STYLE_STATES[THEME_ST_COUNT] = {"normal", "hover", "pressed", "disabled"};

static uint8_t state_bit(const json_doc *doc, int tok)
{
    uint32_t i;
    for (i = 0; i < THEME_LS_COUNT; i++)
        if (json_string_eq(doc, tok, LAUNCHER_STATES[i])) return (uint8_t)(1u << i);
    return 0; /* a state this launcher never enters */
}

static uint8_t parse_visible_when(theme_ctx *c, int tok)
{
    uint8_t mask = 0;
    int s;
    if (tok < 0) return (uint8_t)THEME_LS_ALL;
    if (json_kind(c->doc, tok) == JSON_STRING) return state_bit(c->doc, tok);
    if (json_kind(c->doc, tok) != JSON_ARRAY) {
        c->theme->warnings |= THEME_WARN_BAD_VALUE;
        return (uint8_t)THEME_LS_ALL;
    }
    for (s = json_first(c->doc, tok); s >= 0; s = json_next(c->doc, s)) mask |= state_bit(c->doc, s);
    return mask;
}

static void parse_align(theme_ctx *c, int tok, uint8_t *align)
{
    if (tok < 0) return;
    if (json_string_eq(c->doc, tok, "left")) *align = THEME_ALIGN_LEFT;
    else if (json_string_eq(c->doc, tok, "center")) *align = THEME_ALIGN_CENTER;
    else if (json_string_eq(c->doc, tok, "right")) *align = THEME_ALIGN_RIGHT;
    else c->theme->warnings |= THEME_WARN_BAD_VALUE;
}

/* Overlays the fields present in a style state object on *s (which holds the inherited
 * values). Invalid values keep the inherited one. */
static void overlay_style(theme_ctx *c, int object, theme_style *s)
{
    uint32_t v;
    int image;
    if (json_kind(c->doc, object) != JSON_OBJECT) return;
    ctx_color(c, json_get(c->doc, object, "fill"), &s->fill);
    ctx_color(c, json_get(c->doc, object, "color"), &s->color);
    ctx_color(c, json_get(c->doc, object, "border"), &s->border);
    ctx_color(c, json_get(c->doc, object, "track"), &s->track);
    if (theme_ctx_uint(c, json_get(c->doc, object, "border_width"), 0, MAX_BORDER, &v)) s->border_width = (uint16_t)v;
    if (theme_ctx_uint(c, json_get(c->doc, object, "radius"), 0, MAX_RADIUS, &v)) s->radius = (uint16_t)v;
    image = json_get(c->doc, object, "image");
    if (json_is_null(c->doc, image)) s->image = -1;
    else if (image >= 0 && (image = theme_ctx_blob(c, image, 0)) >= 0) s->image = (int16_t)image;
    ctx_font(c, json_get(c->doc, object, "font"), &s->font);
}

/* rect [x, y, w, h] of non-negative integers, clipped to the window. 0 if unusable. */
static int parse_rect(theme_ctx *c, int tok, theme_rect *r)
{
    theme_model *t = c->theme;
    uint64_t v[4];
    uint32_t i;
    if (json_kind(c->doc, tok) != JSON_ARRAY || json_count(c->doc, tok) != 4) return 0;
    for (i = 0; i < 4; i++)
        if (!json_u64(c->doc, json_at(c->doc, tok, i), &v[i]) || v[i] > MAX_RECT_COORD) return 0;
    if (v[2] == 0 || v[3] == 0) return 0;
    if (v[0] >= t->width || v[1] >= t->height) {
        t->warnings |= THEME_WARN_CLIPPED; /* entirely outside: nothing left to draw */
        return 0;
    }
    if (v[0] + v[2] > t->width) {
        v[2] = t->width - v[0];
        t->warnings |= THEME_WARN_CLIPPED;
    }
    if (v[1] + v[3] > t->height) {
        v[3] = t->height - v[1];
        t->warnings |= THEME_WARN_CLIPPED;
    }
    r->x = (int32_t)v[0], r->y = (int32_t)v[1], r->w = (int32_t)v[2], r->h = (int32_t)v[3];
    return 1;
}

static void parse_element(theme_ctx *c, int object)
{
    theme_model *t = c->theme;
    const theme_type_entry *type = theme_type_find(c->doc, json_get(c->doc, object, "type"));
    theme_element *e;
    theme_style base;
    int style;
    uint32_t i;

    if (!type) {
        t->warnings |= THEME_WARN_UNKNOWN_TYPE;
        t->skipped_elements++;
        return;
    }
    if (t->element_count >= THEME_MAX_ELEMENTS) {
        t->warnings |= THEME_WARN_LIMIT_ELEMENTS;
        t->skipped_elements++;
        return;
    }
    e = &t->elements[t->element_count];
    zero(e, sizeof *e);
    e->type = type->type;
    e->align = type->align;
    e->image = -1;
    if (!parse_rect(c, json_get(c->doc, object, "rect"), &e->rect)) {
        t->warnings |= THEME_WARN_BAD_ELEMENT;
        t->skipped_elements++;
        return;
    }
    e->text = theme_ctx_string(c, json_get(c->doc, object, "text"));
    e->visible_when = parse_visible_when(c, json_get(c->doc, object, "visible_when"));
    parse_align(c, json_get(c->doc, object, "align"), &e->align);

    /* Base style from the element's own font/colour, then normal, then each state over normal. */
    base.fill = THEME_COLOR_NONE;
    base.border = THEME_COLOR_NONE;
    base.track = DEFAULT_TRACK;
    base.border_width = 0;
    base.radius = 0;
    base.image = -1;
    base.font = t->default_font;
    theme_color_resolve(t, "text", THEME_COLOR_TEXT, &base.color);
    ctx_color(c, json_get(c->doc, object, "color"), &base.color);
    ctx_font(c, json_get(c->doc, object, "font"), &base.font);

    style = json_get(c->doc, object, "style");
    e->style[THEME_ST_NORMAL] = base;
    overlay_style(c, json_get(c->doc, style, STYLE_STATES[THEME_ST_NORMAL]), &e->style[THEME_ST_NORMAL]);
    for (i = 1; i < THEME_ST_COUNT; i++) {
        e->style[i] = e->style[THEME_ST_NORMAL];
        overlay_style(c, json_get(c->doc, style, STYLE_STATES[i]), &e->style[i]);
    }

    if (!type->parse(c, object, e)) {
        t->warnings |= THEME_WARN_BAD_ELEMENT;
        t->skipped_elements++;
        return;
    }
    t->element_count++;
}

/* ---------------------------------------------------------------- public API */

theme_error theme_parse(theme_model *theme, const char *text, size_t len, json_token *tokens,
                        uint32_t cap)
{
    json_doc doc;
    theme_ctx c;
    uint64_t schema;
    int min, elements, e;

    zero(theme, sizeof *theme);
    theme->pool_used = 1; /* pool[0] = "" is reference 0 */
    theme->width = 960;
    theme->height = 600;
    theme->draggable = 1;
    theme->background_image = -1;
    theme->default_font = -1;

    if (json_parse(&doc, text, len, tokens, cap) != JSON_OK) return THEME_ERR_JSON;
    if (json_kind(&doc, 0) != JSON_OBJECT) return THEME_ERR_ROOT;
    if (!json_u64(&doc, json_get(&doc, 0, "schema_version"), &schema) || schema == 0 ||
        schema > THEME_SCHEMA_VERSION)
        return THEME_ERR_SCHEMA;
    theme->schema_version = (uint32_t)schema;

    min = json_get(&doc, 0, "min_launcher_version");
    if (min >= 0) {
        char buf[32];
        if (!read_name(&doc, min, buf, sizeof buf) || !version_parse(buf, &theme->min_launcher))
            return THEME_ERR_VERSION;
    }

    c.theme = theme;
    c.doc = &doc;
    /* Order matters: colours need tokens, the window background needs tokens, elements need
     * the window size, tokens and fonts. */
    parse_tokens(&c, json_get(&doc, 0, "tokens"));
    theme_color_resolve(theme, "bg", THEME_COLOR_BG, &theme->background);
    parse_fonts(&c, json_get(&doc, 0, "fonts"));
    parse_window(&c, json_get(&doc, 0, "window"));
    parse_strings(&c, json_get(&doc, 0, "strings"));

    elements = json_get(&doc, 0, "elements");
    for (e = json_first(&doc, elements); e >= 0; e = json_next(&doc, e)) {
        if (json_kind(&doc, e) != JSON_OBJECT) {
            theme->warnings |= THEME_WARN_BAD_ELEMENT;
            theme->skipped_elements++;
            continue;
        }
        parse_element(&c, e);
    }
    return THEME_OK;
}

int theme_requires_update(const theme_model *theme, const char *my_version)
{
    version mine;
    if (!version_parse(my_version, &mine)) return 0;
    return version_compare(&theme->min_launcher, &mine) > 0;
}

const char *theme_str(const theme_model *theme, theme_sref ref)
{
    if (ref == 0 || ref >= theme->pool_used) return "";
    return theme->pool + ref;
}

int theme_color_resolve(const theme_model *theme, const char *text, theme_color fallback,
                        theme_color *out)
{
    uint16_t i;
    if (text && text[0] == '#') {
        if (parse_hex_color(text, out)) return 1;
    } else if (text) {
        for (i = 0; i < theme->token_count; i++) {
            if (str_eq(theme->tokens[i].name, text)) {
                *out = theme->tokens[i].value;
                return 1;
            }
        }
    }
    *out = fallback;
    return 0;
}

static const theme_lang *lang_find(const theme_model *theme, const char *name, size_t n)
{
    uint16_t i;
    for (i = 0; i < theme->lang_count; i++)
        if (lang_eq_n(theme->langs[i].name, name, n)) return &theme->langs[i];
    return 0;
}

static const char *lang_lookup(const theme_model *theme, const theme_lang *lang, const char *key)
{
    uint16_t i;
    if (!lang) return 0;
    for (i = 0; i < lang->string_count; i++)
        if (str_eq(theme_str(theme, lang->strings[i].key), key)) return theme_str(theme, lang->strings[i].value);
    return 0;
}

/* Up to three candidate languages: lang, its base ("pt" for "pt-BR" / "pt_BR"), "en". */
static uint32_t lang_chain(const theme_model *theme, const char *lang, const theme_lang **out)
{
    uint32_t n = 0;
    if (lang && lang[0]) {
        size_t len = str_len(lang), base = 0;
        out[n++] = lang_find(theme, lang, len);
        while (base < len && lang[base] != '-' && lang[base] != '_') base++;
        if (base < len) out[n++] = lang_find(theme, lang, base);
    }
    out[n++] = lang_find(theme, "en", 2);
    return n;
}

const char *theme_text(const theme_model *theme, const char *lang, const char *key)
{
    const theme_lang *chain[3];
    uint32_t n = lang_chain(theme, lang, chain), i;
    if (!key) return "";
    for (i = 0; i < n; i++) {
        const char *s = lang_lookup(theme, chain[i], key);
        if (s) return s;
    }
    return key;
}

const char *theme_title(const theme_model *theme, const char *lang)
{
    const theme_lang *chain[3];
    uint32_t n = lang_chain(theme, lang, chain), i;
    for (i = 0; i < n; i++)
        if (chain[i] && chain[i]->title) return theme_str(theme, chain[i]->title);
    return "";
}

const theme_element *theme_element_at(const theme_model *theme, uint32_t i)
{
    return i < theme->element_count ? &theme->elements[i] : 0;
}

int theme_element_visible(const theme_element *e, theme_launcher_state ls)
{
    return (unsigned)ls < THEME_LS_COUNT && (e->visible_when >> ls) & 1u;
}

const theme_style *theme_element_style(const theme_element *e, theme_state st)
{
    return &e->style[(unsigned)st < THEME_ST_COUNT ? st : THEME_ST_NORMAL];
}

const theme_font *theme_font_at(const theme_model *theme, int index)
{
    return index >= 0 && index < theme->font_count ? &theme->fonts[index] : 0;
}

uint32_t theme_required_blobs(const theme_model *theme, uint8_t (*out)[SHA256_BYTES], uint32_t cap)
{
    uint32_t i, j;
    /* Blobs are deduplicated as they are registered; only referenced ones are registered. */
    for (i = 0; out && i < theme->blob_count && i < cap; i++)
        for (j = 0; j < SHA256_BYTES; j++) out[i][j] = theme->blobs[i].sha256[j];
    return theme->blob_count;
}

int theme_format(const char *template_text, const theme_vars *vars, char *out, size_t cap)
{
    static const char *const names[] = {"days", "hours", "minutes", "seconds",
                                        "percent", "version", "done", "total"};
    const char *values[8] = {0};
    strbuf sb;
    const char *p = template_text ? template_text : "";
    if (cap == 0) return 0;
    if (vars) {
        values[0] = vars->days, values[1] = vars->hours, values[2] = vars->minutes;
        values[3] = vars->seconds, values[4] = vars->percent, values[5] = vars->version;
        values[6] = vars->done, values[7] = vars->total;
    }
    sb_init(&sb, out, cap);
    while (*p) {
        const char *close;
        uint32_t i;
        int done = 0;
        if (*p != '{') {
            sb_add_char(&sb, *p++);
            continue;
        }
        for (close = p + 1; *close && *close != '}' && *close != '{'; close++) {}
        if (*close == '}') {
            size_t n = (size_t)(close - p - 1);
            for (i = 0; i < 8 && !done; i++) {
                if (values[i] && str_len(names[i]) == n && mem_eq(names[i], p + 1, n)) {
                    sb_add(&sb, values[i]);
                    p = close + 1;
                    done = 1;
                }
            }
        }
        if (!done) sb_add_char(&sb, *p++);
    }
    return sb.ok;
}
