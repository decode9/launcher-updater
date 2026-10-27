/*
 * ui_internal.h - State shared by the window code (ui.c: window, messages, mailbox; ui_res.c:
 * GDI+ images and fonts; ui_draw.c: painting). UI thread only.
 */
#ifndef LAUNCHER_WIN32_UI_INTERNAL_H
#define LAUNCHER_WIN32_UI_INTERNAL_H

#include "gdiplus.h"
#include "ui.h"

#define UI_VERSION_BYTES 64
#define UI_STATUS_BYTES 256

typedef struct {
    GpImage *image; /* decoded, 32bpp PARGB; 0 if missing */
    uint32_t width, height;
} ui_image;

typedef struct {
    /* configuration */
    ui_config cfg;
    char lang[THEME_NAME_BYTES];
    char version[UI_VERSION_BYTES];

    /* window */
    HWND hwnd;
    HDC back_dc;
    HBITMAP back_bitmap, back_old;
    int back_w, back_h;

    /* what is shown */
    theme_launcher_state state;
    uint64_t done, total;
    char status[UI_STATUS_BYTES]; /* "" = status.<state> */
    uint32_t countdown;           /* seconds left at countdown_tick */
    DWORD countdown_tick;
    uint32_t shown_countdown;     /* last painted value, to repaint only on change */
    uint32_t news_count, news_index;
    DWORD news_tick;
    status_news news[STATUS_NEWS_MAX];
    ui_image news_images[STATUS_NEWS_MAX];

    /* mouse */
    int hover;   /* element index under the mouse (interactive only), -1 none */
    int pressed; /* element index the button went down on, -1 none */
    int tracking;

    /* resources of the active theme */
    ui_image images[THEME_MAX_BLOBS];
    GpFontCollection *font_collection;
    GpFont *fonts[THEME_MAX_FONTS];
    GpFont *default_font;

    /* periodic callback */
    ui_tick_fn tick_fn;
    void *tick_ctx;
} ui_window;

extern ui_window g_ui;
extern theme_model g_ui_theme; /* the active theme */

/* ui_res.c */
int ui_res_start(void);
void ui_res_stop(void);
void ui_res_load_theme(void);   /* (re)loads images and fonts of g_ui_theme */
void ui_res_free_theme(void);
void ui_res_load_news(void);    /* (re)loads news images */
void ui_res_free_news(void);
GpFont *ui_res_font(int index); /* theme font index or -1; never 0 after ui_res_load_theme */

/* ui_draw.c */
void ui_draw(GpGraphics *g);
/* Element index at (x, y) that reacts to the mouse in the current state, or -1. */
int ui_hit(int x, int y);
/* Whether element i is enabled (buttons: play only when ready). */
int ui_enabled(int i);
/* Countdown seconds left now (monotonic). */
uint32_t ui_countdown_now(void);

/* UTF-8 -> UTF-16 into a static buffer (UI thread only); returns the number of chars. */
int ui_wide(const char *utf8, wchar_t *out, int cap);

#endif
