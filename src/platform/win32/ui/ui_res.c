/*
 * ui_res.c - GDI+ resources of the active theme: images decoded from blobs (PNG, JPEG...),
 * theme fonts added from memory, system fonts by family name. UI thread only.
 */
#include "core/str.h"
#include "ui_internal.h"

/* Scratch for one image blob while it is decoded (SHCreateMemStream copies it). */
static uint8_t g_blob[UI_BLOB_MAX];
/* Font files stay here while the private font collection uses them. */
static uint8_t g_font_arena[UI_BLOB_MAX];
static uintptr_t g_gdiplus_token;

int ui_res_start(void)
{
    GdiplusStartupInput in = {1, 0, 0, 0};
    return GdiplusStartup(&g_gdiplus_token, &in, 0) == 0;
}

void ui_res_stop(void)
{
    ui_res_free_news();
    ui_res_free_theme();
    if (g_gdiplus_token) GdiplusShutdown(g_gdiplus_token);
    g_gdiplus_token = 0;
}

static void image_free(ui_image *img)
{
    if (img->image) GdipDisposeImage(img->image);
    img->image = 0;
    img->width = img->height = 0;
}

/* Decodes an encoded image and copies it into a 32bpp premultiplied bitmap, so the source
 * stream can go (GDI+ may otherwise decode lazily from it) and drawing is fast. */
static void image_decode(ui_image *out, const void *data, size_t len)
{
    IStream *stream;
    GpImage *src = 0, *dst = 0;
    GpGraphics *g = 0;
    UINT w = 0, h = 0;
    out->image = 0;
    if (!len || len > 0x7FFFFFFF) return;
    stream = SHCreateMemStream(data, (UINT)len);
    if (!stream) return;
    if (GdipCreateBitmapFromStream(stream, &src) == 0 && GdipGetImageWidth(src, &w) == 0 &&
        GdipGetImageHeight(src, &h) == 0 && w && h && w <= 8192 && h <= 8192 &&
        GdipCreateBitmapFromScan0((int)w, (int)h, 0, GP_PIXFMT_32BPP_PARGB, 0, &dst) == 0) {
        if (GdipGetImageGraphicsContext(dst, &g) == 0) {
            GdipDrawImageRectI(g, src, 0, 0, (int)w, (int)h);
            GdipDeleteGraphics(g);
            out->image = dst;
            out->width = w;
            out->height = h;
        } else {
            GdipDisposeImage(dst);
        }
    }
    if (src) GdipDisposeImage(src);
    stream->lpVtbl->Release(stream);
}

static void image_load(ui_image *out, const uint8_t sha[SHA256_BYTES])
{
    size_t len;
    image_free(out);
    if (!g_ui.cfg.load_blob) return;
    len = g_ui.cfg.load_blob(g_ui.cfg.blob_ctx, sha, g_blob, sizeof g_blob);
    if (len) image_decode(out, g_blob, len);
}

static GpFont *font_create(const char *family, uint32_t size, uint32_t weight)
{
    static wchar_t name[THEME_NAME_BYTES * 4];
    GpFontFamily *fam = 0;
    GpFont *font = 0;
    int generic = 0;
    ui_wide(family, name, (int)(sizeof name / sizeof name[0]));
    if (g_ui.font_collection && name[0]) GdipCreateFontFamilyFromName(name, g_ui.font_collection, &fam);
    if (!fam && name[0]) GdipCreateFontFamilyFromName(name, 0, &fam);
    if (!fam) {
        GdipGetGenericFontFamilySansSerif(&fam);
        generic = 1;
    }
    if (!fam) return 0;
    GdipCreateFont(fam, (float)(size ? size : 14), weight >= 600 ? GP_FONT_BOLD : GP_FONT_REGULAR, GP_UNIT_PIXEL,
                   &font);
    if (!generic) GdipDeleteFontFamily(fam); /* the font keeps its own reference */
    return font;
}

void ui_res_free_theme(void)
{
    uint32_t i;
    for (i = 0; i < THEME_MAX_BLOBS; i++) image_free(&g_ui.images[i]);
    for (i = 0; i < THEME_MAX_FONTS; i++) {
        if (g_ui.fonts[i]) GdipDeleteFont(g_ui.fonts[i]);
        g_ui.fonts[i] = 0;
    }
    if (g_ui.default_font) GdipDeleteFont(g_ui.default_font);
    g_ui.default_font = 0;
    if (g_ui.font_collection) GdipDeletePrivateFontCollection(&g_ui.font_collection);
    g_ui.font_collection = 0;
}

void ui_res_load_theme(void)
{
    const theme_model *t = &g_ui_theme;
    size_t arena_used = 0;
    uint32_t i;
    ui_res_free_theme();

    for (i = 0; i < t->blob_count; i++) {
        const theme_blob *b = &t->blobs[i];
        if (!b->is_font) {
            image_load(&g_ui.images[i], b->sha256);
            continue;
        }
        if (!g_ui.cfg.load_blob) continue;
        {
            size_t len = g_ui.cfg.load_blob(g_ui.cfg.blob_ctx, b->sha256, g_font_arena + arena_used,
                                            sizeof g_font_arena - arena_used);
            if (!len) continue;
            if (!g_ui.font_collection && GdipNewPrivateFontCollection(&g_ui.font_collection) != 0) {
                g_ui.font_collection = 0;
                continue;
            }
            if (GdipPrivateAddMemoryFont(g_ui.font_collection, g_font_arena + arena_used, (int)len) == 0)
                arena_used += (len + 3u) & ~(size_t)3u;
        }
    }

    for (i = 0; i < t->font_count; i++) {
        const theme_font *f = &t->fonts[i];
        g_ui.fonts[i] = font_create(theme_str(t, f->family), f->size, f->weight);
    }
    g_ui.default_font = font_create("Segoe UI", 14, 400);
}

GpFont *ui_res_font(int index)
{
    if (index < 0 || index >= THEME_MAX_FONTS || !g_ui.fonts[index]) index = g_ui_theme.default_font;
    if (index >= 0 && index < THEME_MAX_FONTS && g_ui.fonts[index]) return g_ui.fonts[index];
    return g_ui.default_font;
}

void ui_res_free_news(void)
{
    uint32_t i;
    for (i = 0; i < STATUS_NEWS_MAX; i++) image_free(&g_ui.news_images[i]);
}

void ui_res_load_news(void)
{
    uint32_t i;
    ui_res_free_news();
    for (i = 0; i < g_ui.news_count; i++)
        if (g_ui.news[i].has_image) image_load(&g_ui.news_images[i], g_ui.news[i].image);
}
