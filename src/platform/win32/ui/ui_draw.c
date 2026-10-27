/*
 * ui_draw.c - Paints the active theme for the current launcher state (docs/theming.md):
 * window background, then every visible element in order, each with the resolved style of its
 * interaction state. Also the hit test that decides which elements react to the mouse.
 */
#include "core/display.h"
#include "core/str.h"
#include "ui_internal.h"

#define TEXT_BYTES 1024

static GpStringFormat *g_formats[3]; /* per theme_align */

static ARGB argb(theme_color c)
{
    return ((c & 0xFFu) << 24) | (c >> 8);
}

static int visible_color(theme_color c)
{
    return (c & 0xFFu) != 0;
}

static int min_int(int a, int b)
{
    return a < b ? a : b;
}

/* A rectangle with rounded corners (r clamped to half the shorter side; 0 = square). */
static GpPath *round_path(float x, float y, float w, float h, float r)
{
    GpPath *path = 0;
    float d;
    if (GdipCreatePath(GP_FILL_ALTERNATE, &path) != 0) return 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 0.5f) {
        GdipAddPathRectangle(path, x, y, w, h);
        return path;
    }
    d = r * 2;
    GdipAddPathArc(path, x, y, d, d, 180, 90);
    GdipAddPathArc(path, x + w - d, y, d, d, 270, 90);
    GdipAddPathArc(path, x + w - d, y + h - d, d, d, 0, 90);
    GdipAddPathArc(path, x, y + h - d, d, d, 90, 90);
    GdipClosePathFigure(path);
    return path;
}

static void fill_box(GpGraphics *g, theme_color color, int x, int y, int w, int h, int r)
{
    GpBrush *brush = 0;
    GpPath *path;
    if (!visible_color(color) || w <= 0 || h <= 0) return;
    path = round_path((float)x, (float)y, (float)w, (float)h, (float)r);
    if (path && GdipCreateSolidFill(argb(color), &brush) == 0) {
        GdipFillPath(g, brush, path);
        GdipDeleteBrush(brush);
    }
    if (path) GdipDeletePath(path);
}

/* The border is drawn inside the rect, so it never spills into a neighbour. */
static void draw_border(GpGraphics *g, theme_color color, int width, const theme_rect *r, int radius)
{
    GpPen *pen = 0;
    GpPath *path;
    float half = (float)width / 2;
    if (!visible_color(color) || width <= 0) return;
    path = round_path((float)r->x + half, (float)r->y + half, (float)r->w - (float)width, (float)r->h - (float)width,
                      (float)radius - half);
    if (path && GdipCreatePen1(argb(color), (float)width, GP_UNIT_PIXEL, &pen) == 0) {
        GdipDrawPath(g, pen, path);
        GdipDeletePen(pen);
    }
    if (path) GdipDeletePath(path);
}

static const ui_image *theme_image(int blob)
{
    if (blob < 0 || blob >= THEME_MAX_BLOBS || !g_ui.images[blob].image) return 0;
    return &g_ui.images[blob];
}

static void draw_stretched(GpGraphics *g, const ui_image *img, int x, int y, int w, int h)
{
    if (img && w > 0 && h > 0) GdipDrawImageRectI(g, img->image, x, y, w, h);
}

/* Scales the image to cover the rect keeping its proportions; what sticks out is cut, and the
 * corners follow radius. */
static void draw_cover(GpGraphics *g, const ui_image *img, int x, int y, int w, int h, int radius)
{
    int dw, dh;
    GpPath *clip;
    if (!img || w <= 0 || h <= 0) return;
    if ((uint32_t)w * img->height >= (uint32_t)h * img->width) {
        dw = w;
        dh = (int)(img->height * (uint32_t)w / img->width);
    } else {
        dh = h;
        dw = (int)(img->width * (uint32_t)h / img->height);
    }
    clip = round_path((float)x, (float)y, (float)w, (float)h, (float)radius);
    if (clip) GdipSetClipPath(g, clip, GP_COMBINE_INTERSECT);
    GdipDrawImageRectI(g, img->image, x + (w - dw) / 2, y + (h - dh) / 2, dw, dh);
    GdipResetClip(g);
    if (clip) GdipDeletePath(clip);
}

static GpStringFormat *format_for(int align)
{
    static const int gp_align[3] = {GP_ALIGN_NEAR, GP_ALIGN_CENTER, GP_ALIGN_FAR};
    if (align < 0 || align > 2) align = 0;
    if (!g_formats[align] && GdipCreateStringFormat(GP_FORMAT_NOWRAP, 0, &g_formats[align]) == 0) {
        GdipSetStringFormatAlign(g_formats[align], gp_align[align]);
        GdipSetStringFormatLineAlign(g_formats[align], GP_ALIGN_CENTER); /* one line, centred */
        GdipSetStringFormatTrimming(g_formats[align], GP_TRIM_ELLIPSIS_CHAR);
    }
    return g_formats[align];
}

static void draw_text(GpGraphics *g, const char *text, int font, theme_color color, int x, int y, int w, int h,
                      int align)
{
    static wchar_t wide[TEXT_BYTES];
    GpBrush *brush = 0;
    GpFont *gp_font;
    GpStringFormat *format;
    GpRectF rect;
    int n;
    if (!text[0] || !visible_color(color) || w <= 0 || h <= 0) return;
    gp_font = ui_res_font(font);
    format = format_for(align);
    n = ui_wide(text, wide, TEXT_BYTES);
    if (!gp_font || !format || n <= 0 || GdipCreateSolidFill(argb(color), &brush) != 0) return;
    rect.X = (float)x;
    rect.Y = (float)y;
    rect.Width = (float)w;
    rect.Height = (float)h;
    GdipDrawString(g, wide, n, gp_font, &rect, format, brush);
    GdipDeleteBrush(brush);
}

/* Values of the theme variables for this frame. */
static struct {
    display_countdown countdown;
    char percent[12], done[24], total[24];
    theme_vars vars;
} g_frame;

static void frame_vars(void)
{
    display_countdown_split(ui_countdown_now(), &g_frame.countdown);
    u64_to_dec(display_percent(g_ui.done, g_ui.total), g_frame.percent);
    display_size(g_ui.done, g_frame.done, sizeof g_frame.done);
    display_size(g_ui.total, g_frame.total, sizeof g_frame.total);
    g_frame.vars.days = g_frame.countdown.days;
    g_frame.vars.hours = g_frame.countdown.hours;
    g_frame.vars.minutes = g_frame.countdown.minutes;
    g_frame.vars.seconds = g_frame.countdown.seconds;
    g_frame.vars.percent = g_frame.percent;
    g_frame.vars.done = g_frame.done;
    g_frame.vars.total = g_frame.total;
    g_frame.vars.version = g_ui.version;
}

/* Translated text of a key with the variables filled in. */
static const char *element_text(const char *key)
{
    static char out[TEXT_BYTES];
    if (!key[0]) return "";
    theme_format(theme_text(&g_ui_theme, g_ui.lang, key), &g_frame.vars, out, sizeof out);
    return out;
}

static const char *status_key(void)
{
    static const char *const keys[THEME_LS_COUNT] = {"status.checking", "status.locked", "status.ready",
                                                     "status.installing", "status.error"};
    if (g_ui.status[0]) return g_ui.status;
    return keys[g_ui.state < THEME_LS_COUNT ? g_ui.state : 0];
}

int ui_enabled(int i)
{
    const theme_element *e = theme_element_at(&g_ui_theme, (uint32_t)i);
    if (!e) return 0;
    if (e->type != THEME_EL_BUTTON) return 1;
    return display_action_enabled((theme_action)e->action, g_ui.state);
}

static theme_state interaction(int i, const theme_element *e)
{
    if (e->type != THEME_EL_BUTTON) return THEME_ST_NORMAL;
    if (!ui_enabled(i)) return THEME_ST_DISABLED;
    if (g_ui.hover == i) return g_ui.pressed == i ? THEME_ST_PRESSED : THEME_ST_HOVER;
    return THEME_ST_NORMAL;
}

static void draw_news(GpGraphics *g, const theme_element *e, const theme_style *s)
{
    const theme_rect *r = &e->rect;
    const theme_font *font = theme_font_at(&g_ui_theme, s->font);
    const status_news *item;
    const ui_image *img;
    int line = (font ? font->size : 14) * 2;
    if (!g_ui.news_count) {
        draw_text(g, element_text("news.empty"), s->font, s->color, r->x, r->y, r->w, r->h, e->align);
        return;
    }
    item = &g_ui.news[g_ui.news_index % g_ui.news_count];
    img = &g_ui.news_images[g_ui.news_index % g_ui.news_count];
    if (!img->image) img = 0;
    if (img && r->h > line * 2) {
        draw_cover(g, img, r->x, r->y, r->w, r->h - line, s->radius);
        draw_text(g, item->title, s->font, s->color, r->x, r->y + r->h - line, r->w, line, e->align);
    } else {
        draw_text(g, item->title, s->font, s->color, r->x, r->y, r->w, min_int(r->h, line), e->align);
    }
}

static void draw_progress(GpGraphics *g, const theme_element *e, const theme_style *s)
{
    const theme_rect *r = &e->rect;
    int fw = (int)display_fill_width(g_ui.done, g_ui.total, (uint32_t)r->w);
    fill_box(g, s->track, r->x, r->y, r->w, r->h, s->radius);
    if (fw > 0) fill_box(g, s->fill, r->x, r->y, fw, r->h, s->radius);
    draw_border(g, s->border, s->border_width, r, s->radius);
    draw_text(g, element_text(theme_str(&g_ui_theme, e->text)), s->font, s->color, r->x, r->y, r->w, r->h, e->align);
}

static void draw_element(GpGraphics *g, int i, const theme_element *e)
{
    const theme_style *s = theme_element_style(e, interaction(i, e));
    const theme_rect *r = &e->rect;
    const char *text = "";
    if (e->type == THEME_EL_PROGRESS) {
        draw_progress(g, e, s);
        return;
    }
    fill_box(g, s->fill, r->x, r->y, r->w, r->h, s->radius);
    draw_stretched(g, theme_image(s->image), r->x, r->y, r->w, r->h);
    switch (e->type) {
    case THEME_EL_IMAGE:
        draw_stretched(g, theme_image(e->image), r->x, r->y, r->w, r->h);
        break;
    case THEME_EL_NEWS:
        draw_news(g, e, s);
        break;
    case THEME_EL_STATUS:
        text = element_text(status_key());
        break;
    default: /* text, countdown, button */
        text = element_text(theme_str(&g_ui_theme, e->text));
        break;
    }
    draw_border(g, s->border, s->border_width, r, s->radius);
    draw_text(g, text, s->font, s->color, r->x, r->y, r->w, r->h, e->align);
}

void ui_draw(GpGraphics *g)
{
    const theme_model *t = &g_ui_theme;
    uint32_t i, n = theme_element_count(t);
    GdipSetSmoothingMode(g, GP_SMOOTHING_ANTIALIAS);
    GdipSetTextRenderingHint(g, GP_TEXT_ANTIALIAS_GRIDFIT);
    GdipSetInterpolationMode(g, GP_INTERPOLATION_HQ_BICUBIC);
    GdipSetPixelOffsetMode(g, GP_PIXEL_OFFSET_HALF);
    GdipGraphicsClear(g, argb(t->background | 0xFFu)); /* the window itself is opaque */
    draw_stretched(g, theme_image(t->background_image), 0, 0, t->width, t->height);
    frame_vars();
    for (i = 0; i < n; i++) {
        const theme_element *e = theme_element_at(t, i);
        if (e && theme_element_visible(e, g_ui.state)) draw_element(g, (int)i, e);
    }
}

int ui_hit(int x, int y)
{
    const theme_model *t = &g_ui_theme;
    uint32_t i = theme_element_count(t);
    while (i--) {
        const theme_element *e = theme_element_at(t, i);
        if (!e || !theme_element_visible(e, g_ui.state)) continue;
        if (x < e->rect.x || y < e->rect.y || x >= e->rect.x + e->rect.w || y >= e->rect.y + e->rect.h) continue;
        if (e->type == THEME_EL_BUTTON) return (int)i;
        if (e->type == THEME_EL_NEWS && g_ui.news_count && g_ui.news[g_ui.news_index % g_ui.news_count].link[0])
            return (int)i;
    }
    return -1;
}
