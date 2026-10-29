/*
 * ui.c - The launcher window: a borderless WS_POPUP of the theme's size with rounded corners
 * (window region), double-buffered GDI+ painting (or, for window.transparent themes, a layered
 * window with per-pixel alpha presented by UpdateLayeredWindow), mouse tracking for button states, dragging
 * by any spot that is not interactive (WM_NCHITTEST -> HTCAPTION), and the cross-thread
 * mailbox behind the ui_set_* calls (see ui.h).
 *
 * The active news filter (filter buttons) is UI state: the news carousel and news_list show
 * only the items of that category. The language and options buttons open native popup menus
 * (TrackPopupMenu) whose choice is reported through ui_config.on_menu; the window never acts on
 * the launcher itself.
 */
#include "core/display.h"
#include "core/str.h"
#include "platform/win32/rt.h"
#include "ui_internal.h"

#define WM_UI_MAIL (WM_APP + 1)
#define WM_UI_FRAME (WM_APP + 2) /* layered mode: render and present one frame */
#define TIMER_FRAME 1
#define TIMER_TICK 2
#define FRAME_MS 200

ui_window g_ui;
theme_model g_ui_theme;

#define CLASS_BASE L"LauncherWindow"
#define CLASS_CHARS 64

static wchar_t g_class[CLASS_CHARS];
static HWND g_hwnd_shared; /* read by other threads (atomic) to post to the window */
static int g_exit_reported;

/* ---- Mailbox: written by any thread, applied on the UI thread ------------------------- */

enum {
    MAIL_STATE = 1u << 0,
    MAIL_PROGRESS = 1u << 1,
    MAIL_STATUS = 1u << 2,
    MAIL_COUNTDOWN = 1u << 3,
    MAIL_NEWS = 1u << 4,
    MAIL_THEME = 1u << 5,
    MAIL_VERSION = 1u << 6,
    MAIL_CLOSE = 1u << 7,
    MAIL_PLAY = 1u << 8,
    MAIL_LANG = 1u << 9
};

static struct {
    int lock;
    uint32_t dirty;
    theme_launcher_state state;
    int play_enabled;
    uint64_t done, total;
    char status[UI_STATUS_BYTES];
    uint32_t countdown;
    DWORD countdown_tick;
    uint32_t news_count;
    status_news news[STATUS_NEWS_MAX];
    char version[UI_VERSION_BYTES];
    char lang[THEME_NAME_BYTES];
} g_box;
static theme_model g_box_theme;

/* A spin lock is enough: holders only copy memory (at most one theme, ~47 KiB). */
static void box_lock(void)
{
    while (__atomic_exchange_n(&g_box.lock, 1, __ATOMIC_ACQUIRE)) __asm__ volatile("pause");
}

static void box_unlock(void)
{
    __atomic_store_n(&g_box.lock, 0, __ATOMIC_RELEASE);
}

static void box_post(void)
{
    HWND hwnd = __atomic_load_n(&g_hwnd_shared, __ATOMIC_ACQUIRE);
    if (hwnd) PostMessageW(hwnd, WM_UI_MAIL, 0, 0);
}

void ui_set_state(theme_launcher_state state)
{
    if ((unsigned)state >= THEME_LS_COUNT) return;
    box_lock();
    g_box.state = state;
    g_box.status[0] = 0;
    g_box.dirty = (g_box.dirty | MAIL_STATE) & ~(uint32_t)MAIL_STATUS;
    box_unlock();
    box_post();
}

void ui_set_play_enabled(int enabled)
{
    box_lock();
    g_box.play_enabled = enabled != 0;
    g_box.dirty |= MAIL_PLAY;
    box_unlock();
    box_post();
}

void ui_set_progress(uint64_t done, uint64_t total)
{
    box_lock();
    g_box.done = done;
    g_box.total = total;
    g_box.dirty |= MAIL_PROGRESS;
    box_unlock();
    box_post();
}

void ui_set_status(const char *key_or_text)
{
    box_lock();
    str_copy(g_box.status, sizeof g_box.status, key_or_text ? key_or_text : "");
    g_box.dirty |= MAIL_STATUS;
    box_unlock();
    box_post();
}

void ui_set_countdown(int64_t seconds)
{
    DWORD now = GetTickCount(); /* taken here, so a late delivery does not add time */
    box_lock();
    g_box.countdown = seconds <= 0 ? 0 : seconds > 0xFFFFFFFF ? 0xFFFFFFFFu : (uint32_t)seconds;
    g_box.countdown_tick = now;
    g_box.dirty |= MAIL_COUNTDOWN;
    box_unlock();
    box_post();
}

void ui_set_news(const status_news *items, uint32_t count)
{
    if (count > STATUS_NEWS_MAX) count = STATUS_NEWS_MAX;
    box_lock();
    if (count) memcpy(g_box.news, items, count * sizeof *items);
    g_box.news_count = count;
    g_box.dirty |= MAIL_NEWS;
    box_unlock();
    box_post();
}

void ui_set_theme(const theme_model *theme)
{
    box_lock();
    memcpy(&g_box_theme, theme, sizeof g_box_theme);
    g_box.dirty |= MAIL_THEME;
    box_unlock();
    box_post();
}

void ui_set_version(const char *version)
{
    box_lock();
    str_copy(g_box.version, sizeof g_box.version, version ? version : "");
    g_box.dirty |= MAIL_VERSION;
    box_unlock();
    box_post();
}

void ui_set_lang(const char *lang)
{
    box_lock();
    str_copy(g_box.lang, sizeof g_box.lang, lang && lang[0] ? lang : "en");
    g_box.dirty |= MAIL_LANG;
    box_unlock();
    box_post();
}

void ui_close(void)
{
    box_lock();
    g_box.dirty |= MAIL_CLOSE;
    box_unlock();
    box_post();
}

/* ---- Window helpers ------------------------------------------------------------------- */

int ui_wide(const char *utf8, wchar_t *out, int cap)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, cap);
    if (n <= 0) {
        out[0] = 0;
        return 0;
    }
    return n - 1;
}

uint32_t ui_countdown_now(void)
{
    uint32_t secs = (uint32_t)(GetTickCount() - g_ui.countdown_tick) / 1000u;
    return g_ui.countdown > secs ? g_ui.countdown - secs : 0;
}

/* Moves the countdown base forward so the 32-bit tick difference never wraps (49 days). */
static void countdown_rebase(void)
{
    uint32_t secs = (uint32_t)(GetTickCount() - g_ui.countdown_tick) / 1000u;
    if (secs < 60) return;
    g_ui.countdown = g_ui.countdown > secs ? g_ui.countdown - secs : 0;
    g_ui.countdown_tick += secs * 1000u;
}

/* Opaque windows repaint through WM_PAINT. A layered window presented with UpdateLayeredWindow
 * gets no WM_PAINT, so the frame is rendered from a posted WM_UI_FRAME instead; requests made
 * before it is handled share one frame. */
static void redraw(void)
{
    if (!g_ui.hwnd) return;
    if (!g_ui.layered) {
        InvalidateRect(g_ui.hwnd, 0, 0);
        return;
    }
    if (!g_ui.frame_pending) g_ui.frame_pending = PostMessageW(g_ui.hwnd, WM_UI_FRAME, 0, 0) != 0;
}

static void back_free(void)
{
    if (g_ui.back_image) GdipDisposeImage(g_ui.back_image); /* before the DIB memory it wraps */
    g_ui.back_image = 0;
    g_ui.back_bits = 0;
    g_ui.back_layered = 0;
    if (g_ui.back_dc) {
        SelectObject(g_ui.back_dc, g_ui.back_old);
        DeleteObject(g_ui.back_bitmap);
        DeleteDC(g_ui.back_dc);
    }
    g_ui.back_dc = 0;
    g_ui.back_bitmap = g_ui.back_old = 0;
    g_ui.back_w = g_ui.back_h = 0;
}

/* Logical theme pixels -> window (device) pixels. */
static int device_px(int v)
{
    return display_scaled(v, g_ui.scale ? g_ui.scale : 100);
}

/* The layered back buffer: a top-down 32bpp DIB section (what UpdateLayeredWindow reads, alpha
 * premultiplied) shared with a GDI+ PARGB bitmap over the same memory, so GDI+ keeps the alpha
 * it draws (a graphics on the DC would treat the surface as opaque RGB). */
static HBITMAP back_dib(HDC screen, int w, int h)
{
    BITMAPINFO bi;
    HBITMAP bitmap;
    void *bits = 0;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; /* top-down: rows in the order GDI+ expects with a positive stride */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bitmap = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, 0, 0);
    if (!bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        return 0;
    }
    if (GdipCreateBitmapFromScan0(w, h, w * 4, GP_PIXFMT_32BPP_PARGB, (uint8_t *)bits, &g_ui.back_image) != 0) {
        g_ui.back_image = 0;
        DeleteObject(bitmap);
        return 0;
    }
    g_ui.back_bits = bits;
    return bitmap;
}

static int back_ensure(HDC screen)
{
    int w = device_px(g_ui_theme.width), h = device_px(g_ui_theme.height);
    if (g_ui.back_dc && g_ui.back_w == w && g_ui.back_h == h && g_ui.back_layered == g_ui.layered) return 1;
    back_free();
    g_ui.back_dc = CreateCompatibleDC(screen);
    if (g_ui.back_dc) g_ui.back_bitmap = g_ui.layered ? back_dib(screen, w, h) : CreateCompatibleBitmap(screen, w, h);
    if (!g_ui.back_bitmap) {
        if (g_ui.back_dc) DeleteDC(g_ui.back_dc);
        g_ui.back_dc = 0;
        return 0;
    }
    g_ui.back_old = SelectObject(g_ui.back_dc, g_ui.back_bitmap);
    g_ui.back_w = w;
    g_ui.back_h = h;
    g_ui.back_layered = g_ui.layered;
    return 1;
}

/* Draws the frame into the back buffer. */
static int render(HDC screen)
{
    GpGraphics *g = 0;
    if (!back_ensure(screen)) return 0;
    if (g_ui.back_layered ? GdipGetImageGraphicsContext(g_ui.back_image, &g) != 0
                          : GdipCreateFromHDC(g_ui.back_dc, &g) != 0)
        return 0;
    ui_draw(g);
    GdipDeleteGraphics(g); /* flushes into the back buffer */
    return 1;
}

static void paint(void)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(g_ui.hwnd, &ps);
    if (g_ui.layered) redraw(); /* only before the first UpdateLayeredWindow; the frame comes from WM_UI_FRAME */
    else if (dc && render(dc)) BitBlt(dc, 0, 0, g_ui.back_w, g_ui.back_h, g_ui.back_dc, 0, 0, SRCCOPY);
    EndPaint(g_ui.hwnd, &ps);
}

/* Layered mode: renders the frame and hands it to the system with its per-pixel alpha (pixels
 * with alpha 0 are also click-through, by the OS). */
static void present(void)
{
    HDC screen;
    g_ui.frame_pending = 0;
    if (!g_ui.hwnd || !g_ui.layered) return;
    screen = GetDC(0);
    if (screen && render(screen)) {
        POINT src = {0, 0};
        SIZE size = {g_ui.back_w, g_ui.back_h};
        BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(g_ui.hwnd, screen, 0, &size, g_ui.back_dc, &src, 0, &blend, ULW_ALPHA);
    }
    if (screen) ReleaseDC(0, screen);
}

typedef UINT(WINAPI *get_dpi_for_system_fn)(void);

/* DPI of the system (the manifest declares the process system DPI aware, so it does not change
 * while it runs): GetDpiForSystem from Windows 10 1607 on, else LOGPIXELSY of the screen. */
static uint32_t system_dpi(void)
{
    static uint32_t dpi;
    if (!dpi) {
        HANDLE u32 = GetModuleHandleW(L"user32.dll");
        get_dpi_for_system_fn get_dpi = u32 ? (get_dpi_for_system_fn)GetProcAddress(u32, "GetDpiForSystem") : 0;
        if (get_dpi) dpi = get_dpi();
        if (!dpi) {
            HDC dc = GetDC(0);
            int caps = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 0;
            if (dc) ReleaseDC(0, dc);
            dpi = caps > 0 ? (uint32_t)caps : 96;
        }
    }
    return dpi;
}

/* The primary monitor's area not covered by the taskbar (whole screen if unknown). */
static void work_area(RECT *r)
{
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, r, 0) && r->right > r->left && r->bottom > r->top) return;
    r->left = r->top = 0;
    r->right = GetSystemMetrics(SM_CXSCREEN);
    r->bottom = GetSystemMetrics(SM_CYSCREEN);
}

/* The theme's title in the current language (taskbar, Alt+Tab). */
static void apply_title(void)
{
    static wchar_t title[256];
    const char *text = theme_title(&g_ui_theme, g_ui.lang);
    ui_wide(text[0] ? text : "Launcher", title, 256);
    SetWindowTextW(g_ui.hwnd, title);
}

/* Size, corners and title of the active theme. The scale follows the system DPI, lowered so the
 * whole window fits in the work area; the window is centred there when its size changes. */
static void apply_window(int first)
{
    RECT area;
    int w, h, r, layered = g_ui_theme.transparent != 0;
    if (layered != g_ui.layered) { /* the theme decides; a later theme may switch it either way */
        LONG ex = GetWindowLongW(g_ui.hwnd, GWL_EXSTYLE);
        SetWindowLongW(g_ui.hwnd, GWL_EXSTYLE, layered ? ex | (LONG)WS_EX_LAYERED : ex & ~(LONG)WS_EX_LAYERED);
        g_ui.layered = layered;
    }
    work_area(&area);
    g_ui.scale = display_scale_fit(display_scale_from_dpi(system_dpi()), g_ui_theme.width, g_ui_theme.height,
                                   (uint32_t)(area.right - area.left), (uint32_t)(area.bottom - area.top));
    w = device_px(g_ui_theme.width);
    h = device_px(g_ui_theme.height);
    r = device_px(g_ui_theme.radius);
    /* No GetWindowRect import: the old size is the back buffer's. */
    if (first || w != g_ui.back_w || h != g_ui.back_h) {
        int x = area.left + (area.right - area.left - w) / 2, y = area.top + (area.bottom - area.top - h) / 2;
        SetWindowPos(g_ui.hwnd, 0, x > area.left ? x : area.left, y > area.top ? y : area.top, w, h,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    /* CreateRoundRectRgn takes the corner ellipse size; the +1 covers its exclusive edges. A
     * transparent window has no region: its shape is the alpha of what it draws. */
    SetWindowRgn(g_ui.hwnd, r && !layered ? CreateRoundRectRgn(0, 0, w + 1, h + 1, 2 * r, 2 * r) : 0, 1);
    apply_title();
}

/* ---- News filter ---------------------------------------------------------------------- */

uint32_t ui_news_visible(uint8_t out[STATUS_NEWS_MAX])
{
    return display_news_filter(g_ui.news, g_ui.news_count, g_ui.filter, out, STATUS_NEWS_MAX);
}

/* Position of raw index in the filtered list, or -1. */
static int visible_pos(const uint8_t *items, uint32_t n, uint32_t raw)
{
    uint32_t k;
    for (k = 0; k < n; k++)
        if (items[k] == raw) return (int)k;
    return -1;
}

int ui_news_current(void)
{
    uint8_t items[STATUS_NEWS_MAX];
    uint32_t n = ui_news_visible(items);
    if (!n) return -1;
    return visible_pos(items, n, g_ui.news_index) >= 0 ? (int)g_ui.news_index : (int)items[0];
}

/* Whether some filter button of the active theme has this category. */
static int theme_has_filter(const char *category, const char **first)
{
    uint32_t i, n = theme_element_count(&g_ui_theme);
    int found = 0;
    *first = 0;
    for (i = 0; i < n; i++) {
        const theme_element *e = theme_element_at(&g_ui_theme, i);
        const char *cat;
        if (!e || e->type != THEME_EL_BUTTON || e->action != THEME_ACT_FILTER) continue;
        cat = theme_str(&g_ui_theme, e->category);
        if (!*first) *first = cat;
        if (str_eq(cat, category)) found = 1;
    }
    return found;
}

/* After a theme change: the active filter stays if the new theme still has a button for it;
 * else it becomes the first filter button's category ("" = all news without filter buttons). */
static void reset_filter(void)
{
    const char *first;
    if (theme_has_filter(g_ui.filter, &first)) return;
    str_copy(g_ui.filter, sizeof g_ui.filter, first ? first : "");
}

/* The carousel starts over on the first item of the new filter. */
static void set_filter(const char *category)
{
    uint8_t items[STATUS_NEWS_MAX];
    if (str_eq(g_ui.filter, category)) return;
    str_copy(g_ui.filter, sizeof g_ui.filter, category);
    if (ui_news_visible(items)) g_ui.news_index = items[0];
    g_ui.news_tick = GetTickCount();
    g_ui.hover_row = g_ui.pressed_row = -1;
    redraw();
}

static void apply_mail(void)
{
    uint32_t dirty;
    box_lock();
    dirty = g_box.dirty;
    g_box.dirty = 0;
    if (dirty & MAIL_STATE) {
        g_ui.state = g_box.state;
        g_ui.status[0] = 0;
    }
    if (dirty & MAIL_PLAY) g_ui.play_enabled = g_box.play_enabled;
    if (dirty & MAIL_STATUS) str_copy(g_ui.status, sizeof g_ui.status, g_box.status);
    if (dirty & MAIL_PROGRESS) {
        g_ui.done = g_box.done;
        g_ui.total = g_box.total;
    }
    if (dirty & MAIL_COUNTDOWN) {
        g_ui.countdown = g_box.countdown;
        g_ui.countdown_tick = g_box.countdown_tick;
    }
    if (dirty & MAIL_NEWS) {
        /* The carousel stays on the item it shows while that item is still in the list. */
        uint32_t index = status_news_keep_index(g_ui.news, g_ui.news_count, g_ui.news_index, g_box.news,
                                                g_box.news_count);
        int same = index < g_box.news_count && g_ui.news_index < g_ui.news_count &&
                   status_news_item_equal(&g_ui.news[g_ui.news_index], &g_box.news[index]);
        g_ui.news_count = g_box.news_count;
        if (g_box.news_count) memcpy(g_ui.news, g_box.news, g_box.news_count * sizeof g_ui.news[0]);
        g_ui.news_index = index;
        if (!same) g_ui.news_tick = GetTickCount();
    }
    if (dirty & MAIL_VERSION) str_copy(g_ui.version, sizeof g_ui.version, g_box.version);
    if (dirty & MAIL_LANG) str_copy(g_ui.lang, sizeof g_ui.lang, g_box.lang);
    if (dirty & MAIL_THEME) memcpy(&g_ui_theme, &g_box_theme, sizeof g_ui_theme);
    box_unlock();

    /* Slow work (blob loading, decoding) outside the lock. */
    if (dirty & MAIL_THEME) {
        g_ui.hover = g_ui.pressed = g_ui.hover_row = g_ui.pressed_row = -1;
        reset_filter();
        ui_res_load_theme();
        apply_window(0);
    } else if (dirty & MAIL_LANG) {
        apply_title(); /* apply_window sets it with a new theme */
    }
    if (dirty & MAIL_NEWS) ui_res_load_news();
    if (dirty & MAIL_CLOSE) {
        g_exit_reported = 1; /* closing on request: nothing to report */
        DestroyWindow(g_ui.hwnd);
        return;
    }
    if (dirty) redraw();
}

/* Seconds per news item: the interval of the first news element. */
static uint32_t news_interval_ms(void)
{
    uint32_t i, n = theme_element_count(&g_ui_theme);
    for (i = 0; i < n; i++) {
        const theme_element *e = theme_element_at(&g_ui_theme, i);
        if (e && e->type == THEME_EL_NEWS) return (e->interval ? e->interval : 8) * 1000u;
    }
    return 8000;
}

static void frame_timer(void)
{
    countdown_rebase();
    if (ui_countdown_now() != g_ui.shown_countdown) {
        g_ui.shown_countdown = ui_countdown_now();
        redraw();
    }
    if (GetTickCount() - g_ui.news_tick >= news_interval_ms()) {
        /* Next item of the filtered list (from the shown one, wrapping). */
        uint8_t items[STATUS_NEWS_MAX];
        uint32_t n = ui_news_visible(items);
        g_ui.news_tick = GetTickCount();
        if (n > 1) {
            int pos = visible_pos(items, n, (uint32_t)ui_news_current());
            g_ui.news_index = items[((uint32_t)pos + 1) % n];
            redraw();
        }
    }
}

static void open_url(const char *url)
{
    static wchar_t wide[STATUS_LINK_BYTES];
    if (!str_starts_with(url, "https://")) return;
    if (ui_wide(url, wide, STATUS_LINK_BYTES) > 0) ShellExecuteW(g_ui.hwnd, L"open", wide, 0, 0, SW_SHOWNORMAL);
}

void ui_open_path(const char *path)
{
    static wchar_t wide[1024];
    if (path && path[0] && ui_wide(path, wide, 1024) > 0) ShellExecuteW(g_ui.hwnd, L"open", wide, 0, 0, SW_SHOWNORMAL);
}

/* ---- Native menus --------------------------------------------------------------------- */

#define MENU_LANG_BASE 100 /* language i = MENU_LANG_BASE + i */

/* "language.name" of one theme language itself (no fallback to another language), or 0. */
static const char *lang_name(const theme_lang *lang)
{
    uint32_t k;
    for (k = 0; k < lang->string_count; k++)
        if (str_eq(theme_str(&g_ui_theme, lang->strings[k].key), "language.name"))
            return theme_str(&g_ui_theme, lang->strings[k].value);
    return 0;
}

/* Theme text for key, or the built-in English fallback when the theme lacks it. */
static const char *menu_text(const char *key, const char *fallback)
{
    const char *text = theme_text(&g_ui_theme, g_ui.lang, key);
    return str_eq(text, key) ? fallback : text;
}

static void menu_add(HMENU menu, UINT flags, uintptr_t id, const char *text)
{
    static wchar_t wide[256];
    ui_wide(text, wide, 256);
    AppendMenuW(menu, MF_STRING | flags, id, wide);
}

/* Shows the menu under the button (its bottom-left corner, in device pixels then screen
 * coordinates) and returns the chosen id, 0 if none. */
static UINT menu_track(HMENU menu, const theme_element *e)
{
    POINT pt;
    UINT id;
    pt.x = device_px(e->rect.x);
    pt.y = device_px(e->rect.y + e->rect.h);
    ClientToScreen(g_ui.hwnd, &pt);
    SetForegroundWindow(g_ui.hwnd); /* so the menu closes when the user clicks elsewhere */
    id = (UINT)TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY, (int)pt.x,
                              (int)pt.y, 0, g_ui.hwnd, 0);
    PostMessageW(g_ui.hwnd, WM_NULL, 0, 0); /* documented TrackPopupMenu workaround */
    g_ui.hover = g_ui.hover_row = -1; /* the mouse moved over the menu meanwhile */
    redraw();
    return id;
}

static void menu_report(ui_menu_item item, const char *arg)
{
    if (g_ui.cfg.on_menu) g_ui.cfg.on_menu(g_ui.cfg.menu_ctx, item, arg);
}

/* Every language of the theme, named by its own language.name (else its tag), the current one
 * checked. */
static void language_menu(const theme_element *e)
{
    HMENU menu;
    uint32_t i, n = g_ui_theme.lang_count;
    UINT id;
    if (!n || !g_ui.cfg.on_menu || !(menu = CreatePopupMenu())) return;
    for (i = 0; i < n; i++) {
        const theme_lang *lang = &g_ui_theme.langs[i];
        const char *name = lang_name(lang);
        menu_add(menu, str_eq(lang->name, g_ui.lang) ? MF_CHECKED : 0, MENU_LANG_BASE + i,
                 name && name[0] ? name : lang->name);
    }
    id = menu_track(menu, e);
    DestroyMenu(menu);
    if (id >= MENU_LANG_BASE && id < MENU_LANG_BASE + n)
        menu_report(UI_MENU_LANGUAGE, g_ui_theme.langs[id - MENU_LANG_BASE].name);
}

static void options_menu(const theme_element *e)
{
    HMENU menu;
    UINT id;
    if (!g_ui.cfg.on_menu || !(menu = CreatePopupMenu())) return;
    menu_add(menu, display_repair_enabled(g_ui.state) ? 0 : MF_GRAYED, UI_MENU_REPAIR,
             menu_text("menu.repair", "Verify and repair files"));
    menu_add(menu, 0, UI_MENU_OPEN_FOLDER, menu_text("menu.open_folder", "Open game folder"));
    if (g_ui.cfg.has_settings)
        menu_add(menu, display_settings_enabled(g_ui.state) ? 0 : MF_GRAYED, UI_MENU_GAME_SETTINGS,
                 menu_text("menu.game_settings", "Game settings"));
    id = menu_track(menu, e);
    DestroyMenu(menu);
    /* The state may have changed while the menu was open: check again. */
    if (id == UI_MENU_REPAIR && !display_repair_enabled(g_ui.state)) return;
    if (id == UI_MENU_GAME_SETTINGS && !display_settings_enabled(g_ui.state)) return;
    if (id >= UI_MENU_REPAIR && id <= UI_MENU_GAME_SETTINGS) menu_report((ui_menu_item)id, "");
}

static void report_exit(void)
{
    if (g_exit_reported) return;
    g_exit_reported = 1;
    if (g_ui.cfg.on_action) g_ui.cfg.on_action(g_ui.cfg.action_ctx, THEME_ACT_EXIT);
}

/* row: the news_list row clicked (raw rows of the filtered list), else -1. */
static void activate(int i, int row)
{
    const theme_element *e = theme_element_at(&g_ui_theme, (uint32_t)i);
    if (!e || !ui_enabled(i)) return;
    if (e->type == THEME_EL_NEWS) {
        int current = ui_news_current();
        if (current >= 0) open_url(g_ui.news[current].link);
        return;
    }
    if (e->type == THEME_EL_NEWS_LIST) {
        uint8_t items[STATUS_NEWS_MAX];
        if (row >= 0 && (uint32_t)row < ui_news_visible(items)) open_url(g_ui.news[items[row]].link);
        return;
    }
    switch ((theme_action)e->action) {
    case THEME_ACT_PLAY:
        if (g_ui.cfg.on_action) g_ui.cfg.on_action(g_ui.cfg.action_ctx, THEME_ACT_PLAY);
        break;
    case THEME_ACT_EXIT:
        report_exit();
        DestroyWindow(g_ui.hwnd);
        break;
    case THEME_ACT_MINIMIZE:
        ShowWindow(g_ui.hwnd, SW_MINIMIZE);
        break;
    case THEME_ACT_OPEN_URL:
        open_url(theme_str(&g_ui_theme, e->url));
        break;
    case THEME_ACT_FILTER:
        set_filter(theme_str(&g_ui_theme, e->category));
        break;
    case THEME_ACT_LANGUAGE:
        language_menu(e);
        break;
    case THEME_ACT_OPTIONS:
        options_menu(e);
        break;
    }
}

static void set_hover(int i, int row)
{
    if (g_ui.hover == i && g_ui.hover_row == row) return;
    g_ui.hover = i;
    g_ui.hover_row = row;
    redraw();
}

static int lparam_x(LPARAM lp)
{
    return (int)(short)(lp & 0xFFFF);
}

static int lparam_y(LPARAM lp)
{
    return (int)(short)((lp >> 16) & 0xFFFF);
}

static LRESULT WINAPI window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_UI_MAIL:
        apply_mail();
        return 0;
    case WM_TIMER:
        if (wp == TIMER_FRAME) frame_timer();
        else if (wp == TIMER_TICK && g_ui.tick_fn) g_ui.tick_fn(g_ui.tick_ctx);
        return 0;
    case WM_PAINT:
        paint();
        return 0;
    case WM_UI_FRAME:
        present();
        return 0;
    case WM_ERASEBKGND:
        return 1; /* everything is painted from the back buffer */
    case WM_NCHITTEST: {
        POINT pt;
        pt.x = lparam_x(lp);
        pt.y = lparam_y(lp);
        ScreenToClient(hwnd, &pt);
        if (ui_hit((int)pt.x, (int)pt.y, 0) >= 0 || !g_ui_theme.draggable) return HTCLIENT;
        return HTCAPTION;
    }
    case WM_NCMOUSEMOVE:
        set_hover(-1, -1);
        break;
    case WM_NCLBUTTONDBLCLK:
        return 0; /* a double click on the drag area must not maximise */
    case WM_MOUSEMOVE: {
        int row, i;
        if (!g_ui.tracking) {
            TRACKMOUSEEVENT tme = {sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
            g_ui.tracking = TrackMouseEvent(&tme) != 0;
        }
        i = ui_hit(lparam_x(lp), lparam_y(lp), &row);
        set_hover(i, row);
        return 0;
    }
    case WM_MOUSELEAVE:
        g_ui.tracking = 0;
        set_hover(-1, -1);
        return 0;
    case WM_LBUTTONDOWN: {
        int row, i = ui_hit(lparam_x(lp), lparam_y(lp), &row);
        if (i >= 0 && ui_enabled(i)) {
            g_ui.pressed = i;
            g_ui.pressed_row = row;
            SetCapture(hwnd);
            redraw();
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        int row, i = g_ui.pressed, pressed_row = g_ui.pressed_row;
        if (i < 0) return 0;
        g_ui.pressed = g_ui.pressed_row = -1;
        ReleaseCapture();
        redraw();
        if (ui_hit(lparam_x(lp), lparam_y(lp), &row) == i && row == pressed_row) activate(i, row);
        return 0;
    }
    case WM_CAPTURECHANGED: /* lp: the window taking the capture (ours on SetCapture in Wine) */
        if (g_ui.pressed >= 0 && (HWND)lp != hwnd) {
            g_ui.pressed = g_ui.pressed_row = -1;
            redraw();
        }
        return 0;
    case WM_SETCURSOR:
        if ((lp & 0xFFFF) == HTCLIENT) {
            int hand = g_ui.hover >= 0 && ui_enabled(g_ui.hover);
            SetCursor(LoadCursorW(0, hand ? IDC_HAND : IDC_ARROW));
            return 1;
        }
        break;
    case WM_CLOSE: /* Alt+F4, taskbar "Close window" */
        report_exit();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        __atomic_store_n(&g_hwnd_shared, (HWND)0, __ATOMIC_RELEASE);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---- Lifetime ------------------------------------------------------------------------- */

/* out = "LauncherWindow" or "LauncherWindow-<key>" (key is ASCII, truncated to fit). */
static void class_name(wchar_t out[CLASS_CHARS], const char *key)
{
    size_t n = sizeof CLASS_BASE / sizeof(wchar_t) - 1, i;
    memcpy(out, CLASS_BASE, sizeof CLASS_BASE);
    if (!key || !key[0]) return;
    out[n++] = '-';
    for (i = 0; key[i] && n < CLASS_CHARS - 1; i++) out[n++] = (wchar_t)(unsigned char)key[i];
    out[n] = 0;
}

int ui_bring_to_front(const char *instance_key)
{
    wchar_t name[CLASS_CHARS];
    HWND hwnd;
    class_name(name, instance_key);
    hwnd = FindWindowW(name, 0);
    if (!hwnd) return 0;
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
    return 1;
}

int ui_create(const theme_model *theme, const ui_config *cfg)
{
    WNDCLASSEXW wc;
    memset(&g_ui, 0, sizeof g_ui);
    g_ui.cfg = *cfg;
    g_ui.hover = g_ui.pressed = g_ui.hover_row = g_ui.pressed_row = -1;
    g_ui.state = THEME_LS_CHECKING;
    g_ui.play_enabled = 1;
    g_ui.news_tick = GetTickCount();
    g_ui.countdown_tick = GetTickCount();
    str_copy(g_ui.lang, sizeof g_ui.lang, cfg->lang && cfg->lang[0] ? cfg->lang : "en");
    str_copy(g_ui.version, sizeof g_ui.version, cfg->version ? cfg->version : "");
    memcpy(&g_ui_theme, theme, sizeof g_ui_theme);
    reset_filter();
    if (!ui_res_start()) return 0;

    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = UI_HINSTANCE;
    wc.hIcon = LoadIconW(UI_HINSTANCE, (const wchar_t *)(uintptr_t)1); /* optional resource 1 */
    wc.hCursor = LoadCursorW(0, IDC_ARROW);
    class_name(g_class, cfg->instance_key);
    wc.lpszClassName = g_class;
    if (!RegisterClassExW(&wc)) return 0;
    g_ui.hwnd = CreateWindowExW(WS_EX_APPWINDOW, g_class, L"", WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX, 0, 0,
                                theme->width, theme->height, 0, 0, UI_HINSTANCE, 0);
    if (!g_ui.hwnd) return 0;

    ui_res_load_theme();
    apply_window(1);
    SetTimer(g_ui.hwnd, TIMER_FRAME, FRAME_MS, 0);
    __atomic_store_n(&g_hwnd_shared, g_ui.hwnd, __ATOMIC_RELEASE);
    apply_mail(); /* anything set before the window existed */
    if (g_ui.layered) present(); /* a layered window shows nothing until its first frame */
    ShowWindow(g_ui.hwnd, SW_SHOW);
    UpdateWindow(g_ui.hwnd);
    SetForegroundWindow(g_ui.hwnd);
    return 1;
}

void ui_set_tick(ui_tick_fn fn, void *ctx, uint32_t ms)
{
    g_ui.tick_fn = fn;
    g_ui.tick_ctx = ctx;
    if (!g_ui.hwnd) return;
    if (fn && ms) SetTimer(g_ui.hwnd, TIMER_TICK, ms, 0);
    else KillTimer(g_ui.hwnd, TIMER_TICK);
}

int ui_run(void)
{
    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    back_free();
    ui_res_stop();
    g_ui.hwnd = 0;
    return 0;
}
