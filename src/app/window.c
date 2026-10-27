/*
 * window.c - launcher.exe without arguments: the themed window (docs/spec.md, "Launcher flow").
 *
 * The UI thread (this one) owns the window; one worker thread does every network request and
 * the install, in series, because the HTTP client is not reentrant:
 *
 *   status -> self-update when a newer launcher is offered -> theme (when its SHA-256 changed:
 *   theme.json + every blob it needs, verified, into .launcher/cache/, then ui_set_theme) ->
 *   news images -> state:
 *     closed  -> locked with the countdown; status again every 5 minutes and when it reaches 0;
 *     open    -> ready ("update available" when the release is not installed yet); status again
 *                every 5 minutes while waiting, quietly.
 *   Play     -> install with progress if needed, start the game, close the launcher.
 *   No server-> error state, retry every 30 s; ready (offline) instead when the release in
 *               state.json is complete and the last cached status said open (LOCAL rule).
 *
 * The window starts with the cached theme when it is complete, else the built-in neutral one.
 */
#include "app.h"
#include "app_internal.h"
#include "cache.h"
#include "core/str.h"
#include "core/theme.h"
#include "log.h"
#include "platform/win32/proc.h"
#include "platform/win32/rt.h"
#include "platform/win32/sys.h"
#include "platform/win32/thread.h"
#include "platform/win32/ui/ui.h"
#include "selfupdate.h"

extern const unsigned char theme_neutral[];
extern const unsigned int theme_neutral_size;

#define THEME_JSON_MAX (256u * 1024u)
#define RECHECK_MS (5u * 60u * 1000u) /* LOCAL: status poll while locked or ready */
#define RETRY_MS (30u * 1000u)        /* LOCAL: retry while the server cannot be reached */
#define MIN_WAIT_MS 5000u             /* LOCAL: floor of the poll when the countdown is due */
#define FOREVER 0xFFFFFFFFu
#define ARG_BYTES 1024

static theme_model g_theme; /* the theme on screen (texts are looked up in it) */
static theme_model g_next;  /* a theme being checked before it replaces g_theme */
static json_token g_tokens[THEME_JSON_TOKENS];
static char g_json[THEME_JSON_MAX];
static char g_status_cache[APP_STATUS_MAX];
static uint8_t g_blobs[THEME_MAX_BLOBS][SHA256_BYTES];
static uint8_t g_theme_sha[SHA256_BYTES];
static int g_theme_known; /* g_theme_sha is the SHA-256 of the theme on screen */
static char g_lang[THEME_NAME_BYTES];
static status_news g_news[STATUS_NEWS_MAX];

static thread_event g_wake;
static thread_handle g_worker;
static volatile int g_exit, g_play, g_cancel;
static int g_skip_update, g_offline;

/* ---- texts ---------------------------------------------------------------------------- */

/* Shows the theme's text for key when the theme has one, else the built-in English fallback. */
static void show(const char *key, const char *fallback)
{
    ui_set_status(str_eq(theme_text(&g_theme, g_lang, key), key) ? fallback : key);
}

static void enter(theme_launcher_state state, const char *key, const char *fallback)
{
    ui_set_state(state);
    if (key) show(key, fallback);
}

/* Waits for a click or ms; returns 0 when the launcher is closing. */
static int pause_ms(uint32_t ms)
{
    if (!g_exit) event_wait(&g_wake, ms);
    return !g_exit;
}

/* ---- theme ---------------------------------------------------------------------------- */

static int blobs_cached(const theme_model *theme, int download)
{
    uint32_t i, n = theme_required_blobs(theme, g_blobs, THEME_MAX_BLOBS);
    if (n > THEME_MAX_BLOBS) return 0;
    for (i = 0; i < n; i++) {
        if (g_exit) return 0;
        if (!(download ? cache_fetch_blob(g_blobs[i], APP_SIZE_UNKNOWN) : cache_has_blob(g_blobs[i]))) return 0;
    }
    return 1;
}

/* The cached theme if it parses, suits this version and has all its blobs; else the neutral. */
static void load_start_theme(void)
{
    size_t len;
    if (cache_read("theme.json", g_json, sizeof g_json, &len) &&
        theme_parse(&g_theme, g_json, len, g_tokens, THEME_JSON_TOKENS) == THEME_OK &&
        !theme_requires_update(&g_theme, LAUNCHER_VERSION) && blobs_cached(&g_theme, 0)) {
        sha256(g_json, len, g_theme_sha);
        g_theme_known = 1;
        log_line("theme: cached");
        return;
    }
    theme_parse(&g_theme, (const char *)theme_neutral, theme_neutral_size, g_tokens, THEME_JSON_TOKENS);
    log_line("theme: built-in");
}

static void refresh_theme(void)
{
    const status_blob *t = &app_status.theme;
    size_t len = 0;
    strbuf *sb;
    if (!t->present || (g_theme_known && mem_eq(g_theme_sha, t->sha256, SHA256_BYTES))) return;
    if (t->size > sizeof g_json || !app_fetch_blob(t->sha256, t->size, g_json, sizeof g_json, &len)) {
        log_line("error: theme: download failed or SHA-256 mismatch; keeping the current theme");
        return;
    }
    if (theme_parse(&g_next, g_json, len, g_tokens, THEME_JSON_TOKENS) != THEME_OK) {
        log_line("error: theme: rejected; keeping the current theme");
        return;
    }
    if (theme_requires_update(&g_next, LAUNCHER_VERSION)) {
        sb = log_begin();
        sb_add(sb, "theme: needs launcher ");
        sb_add_u64(sb, g_next.min_launcher.major);
        sb_add_char(sb, '.');
        sb_add_u64(sb, g_next.min_launcher.minor);
        sb_add_char(sb, '.');
        sb_add_u64(sb, g_next.min_launcher.patch);
        sb_add(sb, "; keeping the current theme");
        log_end();
        return;
    }
    if (!blobs_cached(&g_next, 1)) {
        log_line("error: theme: missing blobs; keeping the current theme");
        return;
    }
    if (!cache_save("theme.json", g_json, len)) log_line("error: theme: cannot cache theme.json");
    memcpy(&g_theme, &g_next, sizeof g_theme);
    memcpy(g_theme_sha, t->sha256, SHA256_BYTES);
    g_theme_known = 1;
    ui_set_theme(&g_theme);
    sb = log_begin();
    sb_add(sb, "theme: applied ");
    sb_add_hex(sb, t->sha256, SHA256_BYTES);
    log_end();
}

/* News whose image cannot be cached are shown without it. */
static void refresh_news(void)
{
    uint32_t i, n = app_status.news_count;
    memcpy(g_news, app_status.news, n * sizeof g_news[0]);
    for (i = 0; i < n && !g_exit; i++)
        if (g_news[i].has_image && !cache_fetch_blob(g_news[i].image, APP_SIZE_UNKNOWN)) g_news[i].has_image = 0;
    ui_set_news(g_news, n);
}

/* ---- play ----------------------------------------------------------------------------- */

static void on_progress(void *user, const install_progress *p)
{
    (void)user;
    app_log_progress(p);
    if (p->phase == INSTALL_PHASE_DOWNLOAD || p->phase == INSTALL_PHASE_VERIFY) ui_set_progress(p->done, p->total);
}

/* arg with every "{server}" replaced by the configured server. */
static void expand_arg(char *out, size_t cap, const char *arg)
{
    static const char var[] = "{server}";
    strbuf sb;
    sb_init(&sb, out, cap);
    while (*arg) {
        if (str_starts_with(arg, var)) {
            sb_add(&sb, app_config.server);
            arg += sizeof var - 1;
        } else {
            sb_add_char(&sb, *arg++);
        }
    }
}

/* launch.exe is a confined relative path (config_parse); it runs in the launcher folder. */
static int launch_game(void)
{
    static char exe[FS_PATH_BYTES];
    static char args[CONFIG_ARGS_MAX][ARG_BYTES];
    static const char *argv[CONFIG_ARGS_MAX];
    uint32_t i;
    int ok;
    strbuf *sb;
    for (i = 0; i < app_config.launch_argc; i++) {
        expand_arg(args[i], ARG_BYTES, app_config.launch_args[i]);
        argv[i] = args[i];
    }
    ok = fs_join(exe, sizeof exe, app_dir, app_config.launch_exe) &&
         proc_launch(exe, argv, app_config.launch_argc, app_dir);
    sb = log_begin();
    sb_add(sb, ok ? "launch: started " : "error: launch: cannot start ");
    sb_add(sb, app_config.launch_exe);
    sb_add(sb, " (");
    sb_add_u64(sb, app_config.launch_argc);
    sb_add(sb, " args)");
    log_end();
    return ok;
}

/* Returns 1 when the game was started (the launcher then closes). */
static int play(void)
{
    install_result *result;
    int code;
    if (!g_offline && !app_release_is_current()) {
        ui_set_progress(0, 0);
        enter(THEME_LS_INSTALLING, 0, 0);
        code = app_install_release(on_progress, 0, &g_cancel, &result);
        if (g_exit) return 0;
        if (code != 0) {
            enter(THEME_LS_READY, result->error == INSTALL_ERR_SPACE ? "status.no_space" : "status.install_failed",
                  result->error == INSTALL_ERR_SPACE ? "Not enough free disk space for the update."
                                                     : "The update failed. Press Play to try again.");
            return 0;
        }
    }
    if (!launch_game()) {
        enter(THEME_LS_READY, "status.launch_failed", "The game could not be started.");
        return 0;
    }
    return 1;
}

/* ---- worker --------------------------------------------------------------------------- */

static void show_ready(void)
{
    ui_set_version(app_status.release.version); /* also from the cached status when offline */
    if (g_offline) enter(THEME_LS_READY, "status.offline", "Offline: you can play the installed version.");
    else if (app_release_is_current()) enter(THEME_LS_READY, 0, 0);
    else if (app_install_interrupted())
        enter(THEME_LS_READY, "status.resume", "The install was interrupted: Play resumes it.");
    else enter(THEME_LS_READY, "status.update", "An update is available: Play installs it first.");
}

/* Waits in state ready for Play or for the next poll. Returns 1 when the game was started. */
static int wait_ready(uint32_t ms)
{
    g_play = 0;
    show_ready();
    while (pause_ms(ms)) {
        if (!g_play) return 0; /* time to poll again */
        g_play = 0;
        if (play()) return 1;
    }
    return 0;
}

static void wait_locked(void)
{
    int64_t secs = status_seconds_to_open(&app_status);
    uint32_t ms = RECHECK_MS;
    ui_set_countdown(secs);
    enter(THEME_LS_LOCKED, 0, 0);
    if (secs >= 0 && secs < (int64_t)(RECHECK_MS / 1000u)) ms = (uint32_t)secs * 1000u + 1000u;
    if (ms < MIN_WAIT_MS) ms = MIN_WAIT_MS;
    pause_ms(ms);
}

/* No server: offline play when allowed, else the error state; then waits for the retry. */
static int offline(void)
{
    size_t len;
    g_offline = cache_read("status.json", g_status_cache, sizeof g_status_cache, &len) &&
                app_parse_status(g_status_cache, len) && app_status.open && app_release_is_current();
    if (g_offline) {
        log_line("offline: the installed release may be played (last status said open)");
        return wait_ready(RETRY_MS);
    }
    enter(THEME_LS_ERROR, "status.connection", "Cannot reach the server. Retrying in 30 seconds.");
    pause_ms(RETRY_MS);
    return 0;
}

static void worker(void *unused)
{
    size_t len;
    const char *text;
    int started = 0;
    (void)unused;
    if (!app_ports_init() || !cache_init()) {
        enter(THEME_LS_ERROR, "status.folder", "The launcher folder cannot be written.");
        pause_ms(FOREVER);
        return;
    }
    while (!g_exit && !started) {
        if (!app_fetch_status()) {
            started = offline();
            continue;
        }
        g_offline = 0;
        text = app_status_text(&len);
        if (!cache_save("status.json", text, len)) log_line("error: cannot cache status.json");
        if (!g_skip_update && selfupdate_available()) {
            enter(THEME_LS_CHECKING, "status.self_update", "Updating the launcher…");
            if (selfupdate_start()) {
                started = 1;
                break;
            }
            g_skip_update = 1; /* one attempt per run; this version carries on */
        }
        refresh_theme();
        if (g_exit) break;
        refresh_news();
        ui_set_version(app_status.release.version);
        if (!app_status.open) wait_locked();
        else started = wait_ready(RECHECK_MS);
    }
    if (started) ui_close();
}

/* ---- UI thread ------------------------------------------------------------------------ */

static size_t load_blob(void *ctx, const uint8_t sha[SHA256_BYTES], void *buf, size_t cap)
{
    (void)ctx;
    return cache_load_blob(sha, buf, cap);
}

/* Only signals the worker: the UI thread never waits on the network. */
static void on_action(void *ctx, theme_action action)
{
    (void)ctx;
    if (action == THEME_ACT_PLAY) {
        g_play = 1;
    } else if (action == THEME_ACT_EXIT) {
        g_exit = 1;
        g_cancel = 1;
    }
    event_set(&g_wake);
}

int app_window(void)
{
    ui_config cfg;
    int config_ok;
    if (!app_begin("")) return 1;
    g_skip_update = sys_has_flag("--skip-update");
    sys_language(g_lang, sizeof g_lang);
    config_ok = app_load_config();
    selfupdate_cleanup();
    load_start_theme();

    memset(&cfg, 0, sizeof cfg);
    cfg.lang = g_lang;
    cfg.load_blob = load_blob;
    cfg.on_action = on_action;
    if (!event_create(&g_wake) || !ui_create(&g_theme, &cfg)) {
        log_line("error: cannot create the window");
        app_end();
        return 1;
    }
    if (!config_ok) {
        enter(THEME_LS_ERROR, "status.config", "launcher.json is missing or invalid.");
    } else if (!thread_start(&g_worker, worker, 0)) {
        log_line("error: cannot start the worker thread");
        enter(THEME_LS_ERROR, 0, 0);
    }
    ui_run();
    g_exit = 1;
    g_cancel = 1; /* stops an install in progress; the next run resumes it */
    event_set(&g_wake);
    thread_join(&g_worker);
    log_line("exit");
    app_end();
    return 0;
}
