/*
 * window.c - launcher.exe without arguments: the themed window (docs/spec.md, "Launcher flow").
 *
 * The UI thread (this one) owns the window; one worker thread does every network request and
 * the install, in series, because the HTTP client is not reentrant. What to show, when to poll
 * and what Play does are decided by core/flow.c; the worker is its adapter:
 *
 *   fetch:   status -> self-update when a newer launcher is offered -> theme (when its SHA-256
 *            changed: theme.json + every blob it needs, verified, into .launcher/cache/, then
 *            ui_set_theme) -> news images -> facts for the flow. No server: the facts come from
 *            the cached status.json.
 *   wait:    until a click on Play or the poll delay the flow asked for.
 *   install: with progress; launch: start the game; close: the launcher ends.
 *
 * Closing the window asks every download to stop (app_request_quit, g_cancel) and waits up to
 * EXIT_WAIT_MS for the worker; a worker stuck in a blocking call is cut off with the process.
 *
 * The window starts with the cached theme when it is complete, else the built-in neutral one.
 */
#include "app.h"
#include "app_internal.h"
#include "cache.h"
#include "core/flow.h"
#include "core/str.h"
#include "core/theme.h"
#include "log.h"
#include "platform/win32/instance.h"
#include "platform/win32/proc.h"
#include "platform/win32/rt.h"
#include "platform/win32/sys.h"
#include "platform/win32/thread.h"
#include "platform/win32/ui/ui.h"
#include "selfupdate.h"

extern const unsigned char theme_neutral[];
extern const unsigned int theme_neutral_size;

#define THEME_JSON_MAX (256u * 1024u)
#define FOREVER 0xFFFFFFFFu
#define ARG_BYTES 1024
#define EXIT_WAIT_MS 3000u /* after the window closes, how long the worker gets to stop */

static theme_model g_theme; /* the theme on screen (texts are looked up in it) */
static theme_model g_next;  /* a theme being checked before it replaces g_theme */
static json_token g_tokens[THEME_JSON_TOKENS];
static char g_json[THEME_JSON_MAX];
static char g_status_cache[APP_STATUS_MAX];
static uint8_t g_blobs[THEME_MAX_BLOBS][SHA256_BYTES];
static uint8_t g_theme_sha[SHA256_BYTES];
static int g_theme_known; /* g_theme_sha is the SHA-256 of the theme on screen */
static char g_lang[THEME_NAME_BYTES];
static status_news g_news[STATUS_NEWS_MAX];      /* the list on screen */
static status_news g_next_news[STATUS_NEWS_MAX]; /* the list being prepared */
static uint32_t g_news_count;
static int g_news_shown; /* g_news has been handed to the UI */

static thread_event g_wake;
static thread_handle g_worker;
static volatile int g_play, g_cancel;
static int g_skip_update;

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

/* Puts a flow view on screen. */
static void apply_view(const flow_view *view)
{
    if (view->screen == THEME_LS_LOCKED) ui_set_countdown(view->countdown);
    if (view->screen == THEME_LS_INSTALLING) ui_set_progress(0, 0);
    ui_set_play_enabled(view->play_enabled);
    enter(view->screen, view->key, view->fallback);
}

/* A click on Play since the last call (read and cleared at once: none is lost or served twice). */
static int take_play(void)
{
    return __atomic_exchange_n(&g_play, 0, __ATOMIC_ACQ_REL);
}

/* ---- theme ---------------------------------------------------------------------------- */

static int blobs_cached(const theme_model *theme, int download)
{
    uint32_t i, n = theme_required_blobs(theme, g_blobs, THEME_MAX_BLOBS);
    if (n > THEME_MAX_BLOBS) return 0;
    for (i = 0; i < n; i++) {
        if (app_quitting()) return 0;
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

/* 1 if the list on screen shows this image: it is cached and verified already. */
static int image_on_screen(const uint8_t sha[SHA256_BYTES])
{
    uint32_t i;
    for (i = 0; i < g_news_count; i++)
        if (g_news[i].has_image && mem_eq(g_news[i].image, sha, SHA256_BYTES)) return 1;
    return 0;
}

/* News whose image cannot be cached are shown without it (and retried on the next status).
 * The UI reloads every image when it gets a list, so an unchanged list is not sent again. */
static void refresh_news(void)
{
    uint32_t i, n = app_status.news_count;
    status_news *item;
    memcpy(g_next_news, app_status.news, n * sizeof g_next_news[0]);
    for (i = 0; i < n; i++) {
        item = &g_next_news[i];
        if (app_quitting()) return;
        if (item->has_image && !image_on_screen(item->image) && !cache_fetch_blob(item->image, APP_SIZE_UNKNOWN))
            item->has_image = 0;
    }
    if (g_news_shown && status_news_equal(g_news, g_news_count, g_next_news, n)) return;
    memcpy(g_news, g_next_news, n * sizeof g_news[0]);
    g_news_count = n;
    g_news_shown = 1;
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

/* ---- worker --------------------------------------------------------------------------- */

/* No server: the facts of the cached status, if any (it is what Play may use offline). */
static void cached_facts(flow_facts *facts)
{
    size_t len;
    if (!cache_read("status.json", g_status_cache, sizeof g_status_cache, &len) ||
        !app_parse_status(g_status_cache, len)) {
        log_line("status: unreachable and no cached status");
        return;
    }
    facts->known = 1;
    facts->open = app_status.open;
    facts->has_release = app_status.release.present;
    facts->secs_to_open = status_seconds_to_open(&app_status);
    facts->current = app_release_is_current();
    facts->interrupted = app_install_interrupted();
    ui_set_version(app_status.release.version);
    if (facts->open && facts->current) log_line("offline: the installed release may be played (last status said open)");
}

/* Asks the server for the status and gathers the facts the flow decides with. Returns 0 when
 * the launcher must close (a self-update was started). */
static int fetch_facts(flow_facts *facts)
{
    size_t len;
    const char *text;
    memset(facts, 0, sizeof *facts);
    if (!app_fetch_status()) {
        cached_facts(facts);
        return 1;
    }
    text = app_status_text(&len);
    if (!cache_save("status.json", text, len)) log_line("error: cannot cache status.json");
    if (!g_skip_update && selfupdate_available()) {
        enter(THEME_LS_CHECKING, "status.self_update", "Updating the launcher…");
        if (selfupdate_start()) return 0;
        g_skip_update = 1; /* one attempt per run; this version carries on */
    }
    refresh_theme();
    if (app_quitting()) return 1;
    refresh_news();
    ui_set_version(app_status.release.version);
    facts->reachable = facts->known = 1;
    facts->open = app_status.open;
    facts->has_release = app_status.release.present;
    facts->secs_to_open = status_seconds_to_open(&app_status);
    facts->current = app_release_is_current();
    facts->interrupted = app_install_interrupted();
    return 1;
}

/* Spreads the polls of many launchers whose countdown ended at the same time. */
static uint32_t jitter_seed(void)
{
    return sys_ticks_ms() ^ (proc_current_id() * 2654435761u);
}

/* Waits up to ms for Play; a click the flow ignores does not restart the wait. */
static flow_step wait_step(flow_state *f, uint32_t ms)
{
    uint32_t start = sys_ticks_ms(), spent;
    flow_step s;
    while (!app_quitting()) {
        if (take_play()) {
            s = flow_on_play(f);
            if (s.action != FLOW_DO_NONE) return s;
        }
        spent = sys_ticks_ms() - start;
        if (spent >= ms) break;
        event_wait(&g_wake, ms - spent);
    }
    return flow_on_timeout(f);
}

static flow_step install_step(flow_state *f)
{
    install_result *result;
    if (app_install_release(on_progress, 0, &g_cancel, &result) == 0) return flow_on_install(f, FLOW_INSTALL_OK);
    return flow_on_install(f, result->error == INSTALL_ERR_SPACE ? FLOW_INSTALL_NO_SPACE : FLOW_INSTALL_FAILED);
}

/* Drives the flow until the game starts, a self-update takes over or the launcher closes. */
static void run_flow(void)
{
    flow_state f;
    flow_facts facts;
    flow_step step = flow_start(&f);
    while (!app_quitting()) {
        if (step.show) apply_view(&step.view);
        switch (step.action) {
        case FLOW_DO_FETCH:
            if (!fetch_facts(&facts)) {
                ui_close();
                return;
            }
            if (app_quitting()) return;
            if (take_play()) flow_on_play(&f); /* clicked while fetching: this status serves it */
            step = flow_on_status(&f, &facts, jitter_seed());
            break;
        case FLOW_DO_WAIT: step = wait_step(&f, step.wait_ms); break;
        case FLOW_DO_INSTALL: step = install_step(&f); break;
        case FLOW_DO_LAUNCH: step = flow_on_launch(&f, launch_game()); break;
        case FLOW_DO_CLOSE: ui_close(); return;
        case FLOW_DO_NONE: return; /* never returned as a next step */
        }
    }
}

static void worker(void *unused)
{
    (void)unused;
    if (!app_ports_init() || !cache_init()) {
        enter(THEME_LS_ERROR, "status.folder", "The launcher folder cannot be written.");
        if (!app_quitting()) event_wait(&g_wake, FOREVER);
        return;
    }
    run_flow();
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
        app_request_quit();
        g_cancel = 1;
    }
    event_set(&g_wake);
}

int app_window(void)
{
    ui_config cfg;
    int config_ok;
    if (!app_begin("")) return 1;
    if (!app_lock(0)) {
        log_line(ui_bring_to_front(instance_key()) ? "exit: brought the running launcher to the front"
                                                   : "exit: the running launcher has no window");
        app_end();
        return 0;
    }
    g_skip_update = sys_has_flag("--skip-update");
    sys_language(g_lang, sizeof g_lang);
    config_ok = app_load_config();
    selfupdate_cleanup();
    load_start_theme();

    memset(&cfg, 0, sizeof cfg);
    cfg.lang = g_lang;
    cfg.load_blob = load_blob;
    cfg.on_action = on_action;
    cfg.instance_key = instance_key();
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
    app_request_quit(); /* downloads stop at their next chunk, retries and self-update stop */
    g_cancel = 1;       /* stops an install at its next block; the next run resumes it */
    event_set(&g_wake);
    if (!thread_join_for(&g_worker, EXIT_WAIT_MS)) {
        /* Stuck in a blocking call (DNS has no timeout; a socket read waits up to 15 s).
         * Nothing on disk needs it to finish: the install is journaled, cached files are written
         * to ".part" and renamed. The worker may be logging, so this thread does not; the
         * kernel releases the single-instance mutex only when the worker is gone too. */
        proc_terminate_self(0);
    }
    log_line("exit");
    app_unlock();
    app_end();
    return 0;
}
