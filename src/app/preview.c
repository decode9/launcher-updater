/*
 * preview.c - launcher.exe --theme-preview [folder]: shows a theme without a server.
 *
 * Reads <folder>/theme.json and finds its blobs by hashing every file in the folder and its
 * sub-folders (two levels), so a theme folder works as it is (examples/theme-demo keeps its
 * images by name in images/). Without a folder it shows the built-in neutral theme. The
 * launcher states then cycle (checking -> locked with a countdown -> ready -> installing with
 * a growing progress -> error), with sample news, so every element can be seen.
 *
 *   --preview-state <state>  hold one state (checking, locked, ready, installing, error)
 *   --lang <code>            language of the texts instead of the Windows one
 *
 * The language and options menus work too: a language is applied (not saved), "open game
 * folder" opens the theme folder, repair and game settings only show a status text.
 *
 * Problems (rejected theme.json, missing blobs) are shown as the status text.
 */
#include "preview.h"
#include "prefs.h"

#include "core/sha256.h"
#include "core/str.h"
#include "core/theme.h"
#include "platform/win32/dir.h"
#include "platform/win32/fs.h"
#include "platform/win32/rt.h"
#include "platform/win32/sys.h"
#include "platform/win32/ui/ui.h"

extern const unsigned char theme_neutral[];
extern const unsigned int theme_neutral_size;

#define MAX_FILES 64
#define JSON_BYTES (256u * 1024u)
#define TICK_MS 100

typedef struct {
    uint8_t sha256[SHA256_BYTES];
    char path[FS_PATH_BYTES];
} preview_file;

static theme_model g_theme;
static char g_lang[PREFS_LANG_BYTES];
static json_token g_tokens[THEME_JSON_TOKENS];
static char g_json[JSON_BYTES];
static uint8_t g_file[UI_BLOB_MAX];
static preview_file g_files[MAX_FILES];
static uint32_t g_file_count;
static status_news g_news[6];
static char g_dir[FS_PATH_BYTES]; /* the --theme-preview folder, "" = built-in theme */

static const char *const STATE_NAMES[THEME_LS_COUNT] = {"checking", "locked", "ready", "installing", "error"};
/* Seconds each state lasts in the cycle. */
static const uint32_t STATE_SECONDS[THEME_LS_COUNT] = {3, 6, 6, 8, 4};

static struct {
    int hold;                 /* 1: stay in state */
    theme_launcher_state state;
    uint32_t ticks;           /* ticks spent in state */
    uint64_t done, total;
    char note[160];           /* shown as status when non-empty */
} g_run;

static int add_file(void *ctx, const char *path, int is_dir)
{
    int depth = (int)(intptr_t)ctx;
    size_t len;
    if (is_dir) {
        if (depth < 2) dir_list(path, add_file, (void *)(intptr_t)(depth + 1));
        return 1;
    }
    if (g_file_count >= MAX_FILES) return 0;
    if (!fs_read_all(path, g_file, sizeof g_file, &len)) return 1;
    sha256(g_file, len, g_files[g_file_count].sha256);
    str_copy(g_files[g_file_count].path, FS_PATH_BYTES, path);
    g_file_count++;
    return 1;
}

static const preview_file *find_file(const uint8_t sha[SHA256_BYTES])
{
    uint32_t i;
    for (i = 0; i < g_file_count; i++)
        if (mem_eq(g_files[i].sha256, sha, SHA256_BYTES)) return &g_files[i];
    return 0;
}

static size_t load_blob(void *ctx, const uint8_t sha[SHA256_BYTES], void *buf, size_t cap)
{
    const preview_file *f = find_file(sha);
    size_t len = 0;
    (void)ctx;
    if (!f || !fs_read_all(f->path, buf, cap, &len)) return 0;
    return len;
}

static void on_action(void *ctx, theme_action action)
{
    (void)ctx;
    if (action == THEME_ACT_PLAY) ui_set_status("Play pressed (preview: nothing is started)");
}

/* Menus work without a launcher folder: the language changes (not saved), the theme folder is
 * opened, and repair / game settings only say what would happen (always listed, so a theme
 * author sees the whole options menu). */
static void on_menu(void *ctx, ui_menu_item item, const char *arg)
{
    (void)ctx;
    switch (item) {
    case UI_MENU_LANGUAGE: preview_set_language(arg); break;
    case UI_MENU_OPEN_FOLDER: ui_open_path(g_dir); break;
    case UI_MENU_REPAIR: ui_set_status("Repair chosen (preview: nothing is verified)"); break;
    case UI_MENU_GAME_SETTINGS: ui_set_status("Game settings chosen (preview: nothing is started)"); break;
    }
}

static void enter_state(theme_launcher_state state)
{
    g_run.state = state;
    g_run.ticks = 0;
    if (state == THEME_LS_LOCKED) ui_set_countdown(2 * 86400 + 3 * 3600 + 4 * 60 + 5);
    if (state == THEME_LS_INSTALLING) {
        g_run.total = 1536ull * 1024 * 1024; /* 1.5 GB */
        g_run.done = g_run.hold ? (g_run.total >> 3) * 3 : 0; /* 37.5 % when held */
        ui_set_progress(g_run.done, g_run.total);
    }
    ui_set_state(state);
    if (g_run.note[0]) ui_set_status(g_run.note);
}

static void tick(void *ctx)
{
    (void)ctx;
    g_run.ticks++;
    if (g_run.state == THEME_LS_INSTALLING && !g_run.hold) {
        /* Reaches the total over the state's duration, in uneven steps like a real download. */
        uint64_t step = (g_run.total >> 6) + ((g_run.ticks & 3) ? 0 : g_run.total >> 7);
        g_run.done = g_run.done + step > g_run.total ? g_run.total : g_run.done + step;
        ui_set_progress(g_run.done, g_run.total);
    }
    if (!g_run.hold && g_run.ticks * TICK_MS >= STATE_SECONDS[g_run.state] * 1000u)
        enter_state((theme_launcher_state)((g_run.state + 1) % THEME_LS_COUNT));
}

/* Sample news: the theme's background image, then another image of the folder, then text only.
 * Categories (info, updates, events) and dates so filter buttons and news_list have content. */
static uint32_t make_news(void)
{
    static const struct {
        const char *title, *category, *date;
    } items[6] = {
        {"Sample news: images are scaled to cover the area", "info", "2026-10-28"},
        {"Second item: the carousel moves every few seconds", "updates", "2026-10-21"},
        {"Third item has no image", "events", "2026-10-15"},
        {"Patch notes: balance changes and fixes", "updates", "2026-10-09"},
        {"Weekend event: double experience", "events", "2026-10-02"},
        {"A news item without a category, link or date", "", ""},
    };
    uint32_t i, n = 6, img = 0;
    memset(g_news, 0, sizeof g_news);
    for (i = 0; i < n; i++) {
        str_copy(g_news[i].title, STATUS_TITLE_BYTES, items[i].title);
        str_copy(g_news[i].category, STATUS_CATEGORY_BYTES, items[i].category);
        str_copy(g_news[i].date, STATUS_DATE_BYTES, items[i].date);
        if (i < n - 1) {
            strbuf sb;
            sb_init(&sb, g_news[i].link, STATUS_LINK_BYTES);
            sb_add(&sb, "https://example.com/news/");
            sb_add_u64(&sb, i + 1);
        }
    }
    if (g_theme.background_image >= 0) {
        memcpy(g_news[0].image, g_theme.blobs[g_theme.background_image].sha256, SHA256_BYTES);
        g_news[0].has_image = 1;
    }
    for (i = 0; i < g_theme.blob_count && !img; i++) {
        if (g_theme.blobs[i].is_font || (int)i == g_theme.background_image) continue;
        memcpy(g_news[1].image, g_theme.blobs[i].sha256, SHA256_BYTES);
        g_news[1].has_image = img = 1;
    }
    return n;
}

static int load_theme(const char *dir)
{
    char path[FS_PATH_BYTES];
    size_t len = 0;
    theme_error err;
    if (dir && dir[0]) {
        dir_list(dir, add_file, (void *)(intptr_t)0);
        if (!fs_join(path, sizeof path, dir, "theme.json") || !fs_read_all(path, g_json, sizeof g_json, &len)) {
            str_copy(g_run.note, sizeof g_run.note, "theme.json not found: showing the built-in theme");
        } else if ((err = theme_parse(&g_theme, g_json, len, g_tokens, THEME_JSON_TOKENS)) != THEME_OK) {
            strbuf sb;
            sb_init(&sb, g_run.note, sizeof g_run.note);
            sb_add(&sb, "theme.json rejected (error ");
            sb_add_u64(&sb, (uint64_t)err);
            sb_add(&sb, "): showing the built-in theme");
        } else {
            uint8_t blobs[THEME_MAX_BLOBS][SHA256_BYTES];
            uint32_t i, n = theme_required_blobs(&g_theme, blobs, THEME_MAX_BLOBS), missing = 0;
            for (i = 0; i < n && i < THEME_MAX_BLOBS; i++)
                if (!find_file(blobs[i])) missing++;
            if (missing) {
                strbuf sb;
                sb_init(&sb, g_run.note, sizeof g_run.note);
                sb_add_u64(&sb, missing);
                sb_add(&sb, " blob(s) of theme.json not found in the folder");
            }
            return 1;
        }
    }
    return theme_parse(&g_theme, (const char *)theme_neutral, theme_neutral_size, g_tokens, THEME_JSON_TOKENS) ==
           THEME_OK;
}

int preview_set_language(const char *lang)
{
    if (!prefs_lang_valid(lang)) return 0;
    str_copy(g_lang, sizeof g_lang, lang);
    ui_set_lang(g_lang);
    return 1;
}

int app_preview(void)
{
    static char hold[16];
    ui_config cfg;
    uint32_t i;
    sys_flag_value("--theme-preview", g_dir, sizeof g_dir);
    /* --lang wins; else the saved choice, which falls back to Windows (no folder: app_begin and
     * prefs_load do not run here, so that is the Windows language today). */
    if (!sys_flag_value("--lang", g_lang, sizeof g_lang) || !g_lang[0]) prefs_language(g_lang, sizeof g_lang);
    if (!load_theme(g_dir)) return 1;

    memset(&cfg, 0, sizeof cfg);
    cfg.lang = g_lang;
    cfg.version = "1.2.3";
    cfg.load_blob = load_blob;
    cfg.on_action = on_action;
    cfg.on_menu = on_menu;
    cfg.has_settings = 1;
    if (!ui_create(&g_theme, &cfg)) return 1;
    ui_set_news(g_news, make_news());

    if (sys_flag_value("--preview-state", hold, sizeof hold))
        for (i = 0; i < THEME_LS_COUNT; i++)
            if (str_eq(hold, STATE_NAMES[i])) {
                g_run.hold = 1;
                g_run.state = (theme_launcher_state)i;
            }
    enter_state(g_run.state);
    ui_set_tick(tick, 0, TICK_MS);
    return ui_run();
}
