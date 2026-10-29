/*
 * ui_draw.c - Paints the active theme for the current launcher state (docs/theming.md):
 * window background, then every visible element in order, each with the resolved style of its
 * interaction state. Also the hit test that decides which elements react to the mouse.
 *
 * The theme is in logical pixels (96 DPI). g_ui.scale maps them to the window's device pixels in
 * exactly two places: the world transform set in ui_draw (so rects, radii, borders, fonts and
 * images all scale together, text and shapes rendered sharp at the final size) and the inverse
 * mapping of the mouse point in ui_hit.
 */
#include "core/display.h"
#include "core/str.h"
#include "platform/win32/rt.h"
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

/* A linear gradient brush across span: GDI+ measures the angle like the theme (0 = left to
 * right, 90 = top to bottom). 3-4 colours are evenly spaced with a preset blend. Flip-tiling
 * keeps antialiased edges from picking up the opposite end's colour. */
static GpBrush *gradient_brush(const theme_gradient *grad, const theme_rect *span)
{
    GpBrush *brush = 0;
    GpRectF rect;
    uint32_t i, n = grad->count;
    if (n < 2 || n > THEME_GRADIENT_MAX_COLORS || span->w <= 0 || span->h <= 0) return 0;
    rect.X = (float)span->x;
    rect.Y = (float)span->y;
    rect.Width = (float)span->w;
    rect.Height = (float)span->h;
    if (GdipCreateLineBrushFromRectWithAngle(&rect, argb(grad->colors[0]), argb(grad->colors[n - 1]),
                                             (float)(grad->angle % 360), 0, GP_WRAP_TILE_FLIP_XY, &brush) != 0)
        return 0;
    if (n > 2) {
        ARGB colors[THEME_GRADIENT_MAX_COLORS];
        float stops[THEME_GRADIENT_MAX_COLORS];
        for (i = 0; i < n; i++) {
            colors[i] = argb(grad->colors[i]);
            stops[i] = (float)i / (float)(n - 1);
        }
        GdipSetLinePresetBlend(brush, colors, stops, (int)n);
    }
    return brush;
}

/* Fills a rounded box with a solid colour or, when grad has colours, with that gradient laid
 * across span (the whole element, so a partial box such as a progress fill shows its part of
 * it). grad may be 0. */
static void fill_paint(GpGraphics *g, theme_color color, const theme_gradient *grad, const theme_rect *span, int x,
                       int y, int w, int h, int r)
{
    GpBrush *brush = 0;
    GpPath *path;
    int gradient = grad && grad->count >= 2;
    if ((!gradient && !visible_color(color)) || w <= 0 || h <= 0) return;
    path = round_path((float)x, (float)y, (float)w, (float)h, (float)r);
    if (!path) return;
    if (gradient) brush = gradient_brush(grad, span);
    else if (GdipCreateSolidFill(argb(color), &brush) != 0) brush = 0;
    if (brush) {
        GdipFillPath(g, brush, path);
        GdipDeleteBrush(brush);
    }
    GdipDeletePath(path);
}

static void fill_box(GpGraphics *g, theme_color color, int x, int y, int w, int h, int r)
{
    fill_paint(g, color, 0, 0, x, y, w, h, r);
}

/* The style's background over rect: its gradient if it has one, else its fill colour. */
static void fill_style(GpGraphics *g, const theme_style *s, const theme_rect *rect)
{
    fill_paint(g, s->fill, &s->gradient, rect, rect->x, rect->y, rect->w, rect->h, s->radius);
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

/* Colour matrix for a style's image brightness (an offset of +-value% of full scale on R, G
 * and B) and opacity (alpha scale), or 0 when both are at their defaults: the plain, faster
 * draw call is used then. Free with GdipDisposeImageAttributes. */
static GpImageAttributes *image_attributes(const theme_style *s)
{
    GpImageAttributes *attr = 0;
    GpColorMatrix m;
    float offset;
    int i;
    if (!s || (s->brightness == 0 && s->opacity >= 100)) return 0;
    memset(&m, 0, sizeof m);
    for (i = 0; i < 5; i++) m.m[i][i] = 1.0f;
    offset = (float)s->brightness / 100.0f;
    m.m[4][0] = m.m[4][1] = m.m[4][2] = offset;
    m.m[3][3] = (float)(s->opacity > 100 ? 100 : s->opacity) / 100.0f;
    if (GdipCreateImageAttributes(&attr) != 0) return 0;
    if (GdipSetImageAttributesColorMatrix(attr, GP_COLOR_ADJUST_DEFAULT, 1, &m, 0, GP_COLOR_MATRIX_FLAGS_DEFAULT) !=
        0) {
        GdipDisposeImageAttributes(attr);
        return 0;
    }
    return attr;
}

/* Draws the whole image into (x, y, w, h) with the style's brightness and opacity (s may be 0). */
static void draw_stretched(GpGraphics *g, const ui_image *img, const theme_style *s, int x, int y, int w, int h)
{
    GpImageAttributes *attr;
    if (!img || w <= 0 || h <= 0 || (s && s->opacity == 0)) return;
    attr = image_attributes(s);
    if (!attr) {
        GdipDrawImageRectI(g, img->image, x, y, w, h);
        return;
    }
    GdipDrawImageRectRectI(g, img->image, x, y, w, h, 0, 0, (int)img->width, (int)img->height, GP_UNIT_PIXEL, attr,
                           0, 0);
    GdipDisposeImageAttributes(attr);
}

/* Scales the image to cover the rect keeping its proportions; what sticks out is cut, and the
 * corners follow radius. */
static void draw_cover(GpGraphics *g, const ui_image *img, const theme_style *s, int x, int y, int w, int h,
                       int radius)
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
    draw_stretched(g, img, s, x + (w - dw) / 2, y + (h - dh) / 2, dw, dh);
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
    return display_action_enabled((theme_action)e->action, g_ui.state, g_ui.play_enabled);
}

/* A filter button whose category is the active filter. */
static int filter_selected(const theme_element *e)
{
    return e->type == THEME_EL_BUTTON && e->action == THEME_ACT_FILTER &&
           str_eq(theme_str(&g_ui_theme, e->category), g_ui.filter);
}

/* The selected filter keeps its look under the mouse; hover and pressed apply to the others.
 * A news_list stays normal as a whole: its hover style goes to the hovered row only. */
static theme_state interaction(int i, const theme_element *e)
{
    if (e->type != THEME_EL_BUTTON) return THEME_ST_NORMAL;
    if (!ui_enabled(i)) return THEME_ST_DISABLED;
    if (filter_selected(e)) return THEME_ST_SELECTED;
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
    int current = ui_news_current();
    if (current < 0) {
        draw_text(g, element_text("news.empty"), s->font, s->color, r->x, r->y, r->w, r->h, e->align);
        return;
    }
    item = &g_ui.news[current];
    img = &g_ui.news_images[current];
    if (!img->image) img = 0;
    if (img && r->h > line * 2) {
        draw_cover(g, img, s, r->x, r->y, r->w, r->h - line, s->radius);
        draw_text(g, item->title, s->font, s->color, r->x, r->y + r->h - line, r->w, line, e->align);
    } else {
        draw_text(g, item->title, s->font, s->color, r->x, r->y, r->w, min_int(r->h, line), e->align);
    }
}

/* Tag text of a category: the theme's news.category.<cat>, else the category itself. */
static const char *category_label(const char *category)
{
    static char key[64];
    const char *text;
    strbuf sb;
    sb_init(&sb, key, sizeof key);
    sb_add(&sb, "news.category.");
    sb_add(&sb, category);
    text = theme_text(&g_ui_theme, g_ui.lang, key);
    return str_eq(text, key) ? category : text;
}

/* Number of whole rows of a news_list (rows that do not fit are not drawn nor hit). */
static uint32_t list_rows(const theme_element *e)
{
    return e->row_height ? (uint32_t)e->rect.h / e->row_height : 0;
}

/* One row of a news_list at y: [tag box] gap [title ... ] gap [date], each column only when
 * its width is set. The tag box is inset vertically by a sixth of the row so it reads as a
 * label, not a stripe. */
static void draw_list_row(GpGraphics *g, const theme_element *e, const theme_style *s, const status_news *item,
                          int y)
{
    static char date[32];
    const theme_rect *r = &e->rect;
    const theme_style *ts = &e->tag_style;
    const char *fmt = theme_str(&g_ui_theme, e->date_format);
    int h = e->row_height, x = r->x, right = r->x + r->w;
    if (e->tag_width > 0) {
        int inset = h / 6;
        theme_rect tag;
        tag.x = x;
        tag.y = y + inset;
        tag.w = min_int(e->tag_width, r->w);
        tag.h = h - 2 * inset;
        if (item->category[0]) {
            fill_style(g, ts, &tag);
            draw_border(g, ts->border, ts->border_width, &tag, ts->radius);
            draw_text(g, category_label(item->category), ts->font, ts->color, tag.x, tag.y, tag.w, tag.h,
                      THEME_ALIGN_CENTER);
        }
        x += tag.w + e->gap;
    }
    if (e->date_width > 0) {
        int dw = min_int(e->date_width, right - x);
        display_news_date(item->date, theme_text(&g_ui_theme, g_ui.lang, fmt[0] ? fmt : THEME_DATE_FORMAT), date,
                          sizeof date);
        draw_text(g, date, s->font, s->color, right - dw, y, dw, h, THEME_ALIGN_RIGHT);
        right -= dw + e->gap;
    }
    draw_text(g, item->title, s->font, s->color, x, y, right - x, h, THEME_ALIGN_LEFT);
}

/* The filtered news as rows from the top of the rect; the hovered (or pressed) row gets that
 * style's fill and text colour behind/in that row only. */
static void draw_news_list(GpGraphics *g, int i, const theme_element *e, const theme_style *s)
{
    uint8_t items[STATUS_NEWS_MAX];
    uint32_t row, n = ui_news_visible(items), max = list_rows(e);
    const theme_rect *r = &e->rect;
    if (!n) {
        draw_text(g, element_text("news.empty"), s->font, s->color, r->x, r->y, r->w, r->h, e->align);
        return;
    }
    for (row = 0; row < n && row < max; row++) {
        const theme_style *rs = s;
        int y = r->y + (int)row * e->row_height;
        if (g_ui.hover == i && g_ui.hover_row == (int)row) {
            theme_rect band;
            int pressed = g_ui.pressed == i && g_ui.pressed_row == (int)row;
            rs = theme_element_style(e, pressed ? THEME_ST_PRESSED : THEME_ST_HOVER);
            band.x = r->x;
            band.y = y;
            band.w = r->w;
            band.h = e->row_height;
            fill_style(g, rs, &band);
        }
        draw_list_row(g, e, rs, &g_ui.news[items[row]], y);
    }
}

static void draw_progress(GpGraphics *g, const theme_element *e, const theme_style *s)
{
    const theme_rect *r = &e->rect;
    int fw = (int)display_fill_width(g_ui.done, g_ui.total, (uint32_t)r->w);
    fill_box(g, s->track, r->x, r->y, r->w, r->h, s->radius);
    /* A gradient replaces fill (the done part) and spans the whole bar; the track stays solid. */
    if (fw > 0) fill_paint(g, s->fill, &s->gradient, r, r->x, r->y, fw, r->h, s->radius);
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
    fill_style(g, s, r);
    draw_stretched(g, theme_image(s->image), s, r->x, r->y, r->w, r->h);
    switch (e->type) {
    case THEME_EL_IMAGE:
        draw_stretched(g, theme_image(e->image), s, r->x, r->y, r->w, r->h);
        break;
    case THEME_EL_NEWS:
        draw_news(g, e, s);
        break;
    case THEME_EL_NEWS_LIST:
        draw_news_list(g, i, e, s);
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
    /* An opaque window clears to its background colour at full alpha; a transparent (layered)
     * one keeps the colour's own alpha, so a "none" background leaves see-through pixels.
     * Text stays grayscale antialiased (never ClearType), which keeps the alpha right there. */
    GdipGraphicsClear(g, g_ui.back_layered ? argb(t->background) : argb(t->background | 0xFFu));
    if (g_ui.scale && g_ui.scale != 100) {
        float s = (float)g_ui.scale / 100.0f;
        GdipScaleWorldTransform(g, s, s, GP_MATRIX_ORDER_PREPEND);
    }
    draw_stretched(g, theme_image(t->background_image), 0, 0, 0, t->width, t->height);
    frame_vars();
    for (i = 0; i < n; i++) {
        const theme_element *e = theme_element_at(t, i);
        if (e && theme_element_visible(e, g_ui.state)) draw_element(g, (int)i, e);
    }
}

/* The news_list row at logical y with a link, or -1. */
static int list_hit_row(const theme_element *e, int y)
{
    uint8_t items[STATUS_NEWS_MAX];
    uint32_t n = ui_news_visible(items), row;
    if (!e->row_height) return -1;
    row = (uint32_t)(y - e->rect.y) / e->row_height;
    if (row >= n || row >= list_rows(e) || !g_ui.news[items[row]].link[0]) return -1;
    return (int)row;
}

int ui_hit(int x, int y, int *row)
{
    const theme_model *t = &g_ui_theme;
    uint32_t i = theme_element_count(t);
    int current;
    if (row) *row = -1;
    x = display_unscaled(x, g_ui.scale);
    y = display_unscaled(y, g_ui.scale);
    while (i--) {
        const theme_element *e = theme_element_at(t, i);
        if (!e || !theme_element_visible(e, g_ui.state)) continue;
        if (x < e->rect.x || y < e->rect.y || x >= e->rect.x + e->rect.w || y >= e->rect.y + e->rect.h) continue;
        if (e->type == THEME_EL_BUTTON) return (int)i;
        if (e->type == THEME_EL_NEWS && (current = ui_news_current()) >= 0 && g_ui.news[current].link[0])
            return (int)i;
        if (e->type == THEME_EL_NEWS_LIST) {
            int r = list_hit_row(e, y);
            if (r < 0) continue; /* rows without a link let the click through (drag) */
            if (row) *row = r;
            return (int)i;
        }
    }
    return -1;
}
