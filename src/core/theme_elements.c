/*
 * theme_elements.c - The element type table: one entry per "type" a theme can use.
 *
 * Adding a type: add a value to theme_element_type (theme.h), write its parse function (here
 * or in its own file declared in theme_internal.h) and add one line to TYPES. Common fields
 * (rect, font, color, text, align, visible_when, style) are parsed before the type function
 * runs; it only reads what is specific to the type. Older launchers skip the new type.
 */
#include "theme_internal.h"

#include "str.h"

/* image: "image" is the blob to draw, scaled to rect. Without a usable image there is nothing
 * to draw, so the element is skipped. */
static int parse_image(theme_ctx *c, int object, theme_element *e)
{
    e->image = (int16_t)theme_ctx_blob(c, json_get(c->doc, object, "image"), 0);
    return e->image >= 0;
}

/* text, countdown, status: everything they need is common. */
static int parse_plain(theme_ctx *c, int object, theme_element *e)
{
    (void)c, (void)object, (void)e;
    return 1;
}

/* news: rotates the status news items every "interval" seconds (1..3600, default 8). */
static int parse_news(theme_ctx *c, int object, theme_element *e)
{
    uint32_t interval = 8;
    theme_ctx_uint(c, json_get(c->doc, object, "interval"), 1, 3600, &interval);
    e->interval = (uint16_t)interval;
    return 1;
}

static int is_https_url(const char *s)
{
    size_t i;
    if (!str_starts_with(s, "https://") || !s[8]) return 0;
    for (i = 0; s[i]; i++)
        if ((unsigned char)s[i] <= 0x20 || s[i] == 0x7f || s[i] == '"' || s[i] == '\\') return 0;
    return 1;
}

/* button: "action" is required and must be one this launcher knows (a button that would do
 * nothing is hidden rather than shown dead); open_url also needs an https:// "url". */
static int parse_button(theme_ctx *c, int object, theme_element *e)
{
    static const struct {
        const char *name;
        uint8_t action;
    } actions[] = {
        {"play", THEME_ACT_PLAY},
        {"exit", THEME_ACT_EXIT},
        {"minimize", THEME_ACT_MINIMIZE},
        {"open_url", THEME_ACT_OPEN_URL},
    };
    int tok = json_get(c->doc, object, "action");
    size_t i;
    for (i = 0; i < sizeof actions / sizeof actions[0]; i++) {
        if (!json_string_eq(c->doc, tok, actions[i].name)) continue;
        e->action = actions[i].action;
        if (e->action != THEME_ACT_OPEN_URL) return 1;
        e->url = theme_ctx_string(c, json_get(c->doc, object, "url"));
        return e->url && is_https_url(theme_str(c->theme, e->url));
    }
    return 0;
}

static const theme_type_entry TYPES[] = {
    {"image", THEME_EL_IMAGE, THEME_ALIGN_LEFT, parse_image},
    {"text", THEME_EL_TEXT, THEME_ALIGN_LEFT, parse_plain},
    {"news", THEME_EL_NEWS, THEME_ALIGN_LEFT, parse_news},
    {"progress", THEME_EL_PROGRESS, THEME_ALIGN_CENTER, parse_plain},
    {"countdown", THEME_EL_COUNTDOWN, THEME_ALIGN_LEFT, parse_plain},
    {"status", THEME_EL_STATUS, THEME_ALIGN_LEFT, parse_plain},
    {"button", THEME_EL_BUTTON, THEME_ALIGN_CENTER, parse_button},
};

const theme_type_entry *theme_type_find(const json_doc *doc, int name_tok)
{
    size_t i;
    for (i = 0; i < sizeof TYPES / sizeof TYPES[0]; i++)
        if (json_string_eq(doc, name_tok, TYPES[i].name)) return &TYPES[i];
    return 0;
}
