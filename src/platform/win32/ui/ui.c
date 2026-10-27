/*
 * ui.c - The launcher window: a borderless WS_POPUP of the theme's size with rounded corners
 * (window region), double-buffered GDI+ painting, mouse tracking for button states, dragging
 * by any spot that is not interactive (WM_NCHITTEST -> HTCAPTION), and the cross-thread
 * mailbox behind the ui_set_* calls (see ui.h).
 */
#include "core/str.h"
#include "platform/win32/rt.h"
#include "ui_internal.h"

#define WM_UI_MAIL (WM_APP + 1)
#define TIMER_FRAME 1
#define TIMER_TICK 2
#define FRAME_MS 200

ui_window g_ui;
theme_model g_ui_theme;

static const wchar_t CLASS_NAME[] = L"LauncherWindow";
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
    MAIL_CLOSE = 1u << 7
};

static struct {
    int lock;
    uint32_t dirty;
    theme_launcher_state state;
    uint64_t done, total;
    char status[UI_STATUS_BYTES];
    uint32_t countdown;
    DWORD countdown_tick;
    uint32_t news_count;
    status_news news[STATUS_NEWS_MAX];
    char version[UI_VERSION_BYTES];
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

static void redraw(void)
{
    if (g_ui.hwnd) InvalidateRect(g_ui.hwnd, 0, 0);
}

static void back_free(void)
{
    if (g_ui.back_dc) {
        SelectObject(g_ui.back_dc, g_ui.back_old);
        DeleteObject(g_ui.back_bitmap);
        DeleteDC(g_ui.back_dc);
    }
    g_ui.back_dc = 0;
    g_ui.back_bitmap = g_ui.back_old = 0;
    g_ui.back_w = g_ui.back_h = 0;
}

static int back_ensure(HDC screen)
{
    int w = g_ui_theme.width, h = g_ui_theme.height;
    if (g_ui.back_dc && g_ui.back_w == w && g_ui.back_h == h) return 1;
    back_free();
    g_ui.back_dc = CreateCompatibleDC(screen);
    g_ui.back_bitmap = g_ui.back_dc ? CreateCompatibleBitmap(screen, w, h) : 0;
    if (!g_ui.back_bitmap) {
        if (g_ui.back_dc) DeleteDC(g_ui.back_dc);
        g_ui.back_dc = 0;
        return 0;
    }
    g_ui.back_old = SelectObject(g_ui.back_dc, g_ui.back_bitmap);
    g_ui.back_w = w;
    g_ui.back_h = h;
    return 1;
}

static void paint(void)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(g_ui.hwnd, &ps);
    GpGraphics *g = 0;
    if (dc && back_ensure(dc) && GdipCreateFromHDC(g_ui.back_dc, &g) == 0) {
        ui_draw(g);
        GdipDeleteGraphics(g);
        BitBlt(dc, 0, 0, g_ui.back_w, g_ui.back_h, g_ui.back_dc, 0, 0, SRCCOPY);
    }
    EndPaint(g_ui.hwnd, &ps);
}

/* Size, corners and title of the active theme. Keeps the window's centre when it resizes. */
static void apply_window(int first)
{
    static wchar_t title[256];
    const char *text = theme_title(&g_ui_theme, g_ui.lang);
    int w = g_ui_theme.width, h = g_ui_theme.height, r = g_ui_theme.radius;
    if (first) {
        int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2, y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
        SetWindowPos(g_ui.hwnd, 0, x > 0 ? x : 0, y > 0 ? y : 0, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    } else if (w != g_ui.back_w || h != g_ui.back_h) {
        /* No GetWindowRect import: the old size is the back buffer's, the position is
         * re-centred on the screen like at creation. */
        int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2, y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
        SetWindowPos(g_ui.hwnd, 0, x > 0 ? x : 0, y > 0 ? y : 0, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    /* CreateRoundRectRgn takes the corner ellipse size; the +1 covers its exclusive edges. */
    SetWindowRgn(g_ui.hwnd, r ? CreateRoundRectRgn(0, 0, w + 1, h + 1, 2 * r, 2 * r) : 0, 1);
    ui_wide(text[0] ? text : "Launcher", title, 256);
    SetWindowTextW(g_ui.hwnd, title);
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
        g_ui.news_count = g_box.news_count;
        if (g_box.news_count) memcpy(g_ui.news, g_box.news, g_box.news_count * sizeof g_ui.news[0]);
        g_ui.news_index = 0;
        g_ui.news_tick = GetTickCount();
    }
    if (dirty & MAIL_VERSION) str_copy(g_ui.version, sizeof g_ui.version, g_box.version);
    if (dirty & MAIL_THEME) memcpy(&g_ui_theme, &g_box_theme, sizeof g_ui_theme);
    box_unlock();

    /* Slow work (blob loading, decoding) outside the lock. */
    if (dirty & MAIL_THEME) {
        g_ui.hover = g_ui.pressed = -1;
        ui_res_load_theme();
        apply_window(0);
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
    if (g_ui.news_count > 1 && GetTickCount() - g_ui.news_tick >= news_interval_ms()) {
        g_ui.news_index = (g_ui.news_index + 1) % g_ui.news_count;
        g_ui.news_tick = GetTickCount();
        redraw();
    }
}

static void open_url(const char *url)
{
    static wchar_t wide[STATUS_LINK_BYTES];
    if (!str_starts_with(url, "https://")) return;
    if (ui_wide(url, wide, STATUS_LINK_BYTES) > 0) ShellExecuteW(g_ui.hwnd, L"open", wide, 0, 0, SW_SHOWNORMAL);
}

static void report_exit(void)
{
    if (g_exit_reported) return;
    g_exit_reported = 1;
    if (g_ui.cfg.on_action) g_ui.cfg.on_action(g_ui.cfg.action_ctx, THEME_ACT_EXIT);
}

static void activate(int i)
{
    const theme_element *e = theme_element_at(&g_ui_theme, (uint32_t)i);
    if (!e || !ui_enabled(i)) return;
    if (e->type == THEME_EL_NEWS) {
        if (g_ui.news_count) open_url(g_ui.news[g_ui.news_index % g_ui.news_count].link);
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
    }
}

static void set_hover(int i)
{
    if (g_ui.hover == i) return;
    g_ui.hover = i;
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
    case WM_ERASEBKGND:
        return 1; /* everything is painted from the back buffer */
    case WM_NCHITTEST: {
        POINT pt;
        pt.x = lparam_x(lp);
        pt.y = lparam_y(lp);
        ScreenToClient(hwnd, &pt);
        if (ui_hit((int)pt.x, (int)pt.y) >= 0 || !g_ui_theme.draggable) return HTCLIENT;
        return HTCAPTION;
    }
    case WM_NCMOUSEMOVE:
        set_hover(-1);
        break;
    case WM_NCLBUTTONDBLCLK:
        return 0; /* a double click on the drag area must not maximise */
    case WM_MOUSEMOVE:
        if (!g_ui.tracking) {
            TRACKMOUSEEVENT tme = {sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
            g_ui.tracking = TrackMouseEvent(&tme) != 0;
        }
        set_hover(ui_hit(lparam_x(lp), lparam_y(lp)));
        return 0;
    case WM_MOUSELEAVE:
        g_ui.tracking = 0;
        set_hover(-1);
        return 0;
    case WM_LBUTTONDOWN: {
        int i = ui_hit(lparam_x(lp), lparam_y(lp));
        if (i >= 0 && ui_enabled(i)) {
            g_ui.pressed = i;
            SetCapture(hwnd);
            redraw();
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        int i = g_ui.pressed;
        if (i < 0) return 0;
        g_ui.pressed = -1;
        ReleaseCapture();
        redraw();
        if (ui_hit(lparam_x(lp), lparam_y(lp)) == i) activate(i);
        return 0;
    }
    case WM_CAPTURECHANGED: /* lp: the window taking the capture (ours on SetCapture in Wine) */
        if (g_ui.pressed >= 0 && (HWND)lp != hwnd) {
            g_ui.pressed = -1;
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

int ui_create(const theme_model *theme, const ui_config *cfg)
{
    WNDCLASSEXW wc;
    memset(&g_ui, 0, sizeof g_ui);
    g_ui.cfg = *cfg;
    g_ui.hover = g_ui.pressed = -1;
    g_ui.state = THEME_LS_CHECKING;
    g_ui.news_tick = GetTickCount();
    g_ui.countdown_tick = GetTickCount();
    str_copy(g_ui.lang, sizeof g_ui.lang, cfg->lang && cfg->lang[0] ? cfg->lang : "en");
    str_copy(g_ui.version, sizeof g_ui.version, cfg->version ? cfg->version : "");
    memcpy(&g_ui_theme, theme, sizeof g_ui_theme);
    if (!ui_res_start()) return 0;

    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = UI_HINSTANCE;
    wc.hIcon = LoadIconW(UI_HINSTANCE, (const wchar_t *)(uintptr_t)1); /* optional resource 1 */
    wc.hCursor = LoadCursorW(0, IDC_ARROW);
    wc.lpszClassName = CLASS_NAME;
    if (!RegisterClassExW(&wc)) return 0;
    g_ui.hwnd = CreateWindowExW(WS_EX_APPWINDOW, CLASS_NAME, L"", WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX, 0, 0,
                                theme->width, theme->height, 0, 0, UI_HINSTANCE, 0);
    if (!g_ui.hwnd) return 0;

    ui_res_load_theme();
    apply_window(1);
    SetTimer(g_ui.hwnd, TIMER_FRAME, FRAME_MS, 0);
    __atomic_store_n(&g_hwnd_shared, g_ui.hwnd, __ATOMIC_RELEASE);
    apply_mail(); /* anything set before the window existed */
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
