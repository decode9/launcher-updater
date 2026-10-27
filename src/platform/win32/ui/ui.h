/*
 * ui.h - The launcher window: draws a theme (core/theme.h) with GDI+ and reports clicks.
 *
 * Threads. ui_create, ui_set_tick and ui_run belong to the UI thread (the one that created the
 * window). Every ui_set_* below except ui_set_tick, and ui_close, may be called from any thread:
 * they copy their arguments into a mailbox guarded by a spin lock and post a message, so the
 * caller's buffers can be reused as soon as the call returns. The UI thread applies the latest
 * value of each field on its next message (intermediate values may be skipped, the last one
 * always wins).
 *
 * Callbacks (blob loader, on_action, tick) run on the UI thread and must return quickly: an
 * action handler only signals the worker (event, flag), it does not install anything itself.
 *
 * Typical use from the app (see app/preview.c for a complete example without a server):
 *
 *     ui_config cfg = {lang, version, load_blob, cache, on_action, app};
 *     ui_create(&builtin_theme, &cfg);       // UI thread, window shown in state checking
 *     start worker thread:                    // network + install engine
 *         ui_set_news(st.news, st.news_count);
 *         ui_set_theme(&server_theme);        // once its blobs are in the cache
 *         ui_set_countdown(secs); ui_set_state(THEME_LS_LOCKED);
 *         ui_set_state(THEME_LS_INSTALLING); ui_set_progress(done, total);  // repeatedly
 *         ui_set_state(THEME_LS_READY);
 *     ui_run();                               // until exit / window closed
 *     on_action(PLAY) -> worker starts the game; on_action(EXIT) -> worker stops.
 */
#ifndef LAUNCHER_WIN32_UI_H
#define LAUNCHER_WIN32_UI_H

#include <stddef.h>
#include <stdint.h>

#include "core/status.h"
#include "core/theme.h"

/* Copies the blob with this SHA-256 (an image or a font file the theme references) into buf
 * and returns its size; 0 if it is missing or larger than cap. Called on the UI thread whenever
 * a theme or news list is applied. The UI decodes and verifies nothing but the format: the app
 * has already checked the hash when it cached the blob. */
typedef size_t (*ui_blob_fn)(void *ctx, const uint8_t sha256[SHA256_BYTES], void *buf, size_t cap);

/* A button was clicked: THEME_ACT_PLAY (only possible in state ready) or THEME_ACT_EXIT
 * (the window closes right after the call; Alt+F4 also reports EXIT). minimize and open_url
 * are handled by the window itself and never reported. */
typedef void (*ui_action_fn)(void *ctx, theme_action action);

typedef void (*ui_tick_fn)(void *ctx);

typedef struct {
    const char *lang;     /* UI language for theme texts ("es"); 0 = "en" */
    const char *version;  /* {version} in texts (release version); 0 = empty */
    ui_blob_fn load_blob; /* may be 0: themes then draw without images and theme fonts */
    void *blob_ctx;
    ui_action_fn on_action; /* may be 0 */
    void *action_ctx;
} ui_config;

/* Upper bound of one blob the window loads (images and font files). */
#define UI_BLOB_MAX (8u * 1024u * 1024u)

/* Starts GDI+, creates the window for theme (copied: the caller may drop it) in state
 * checking, and shows it. Returns 1, or 0 if GDI+ or the window could not be created. */
int ui_create(const theme_model *theme, const ui_config *cfg);

/* Runs the message loop until the window is closed; returns 0. Shuts GDI+ down. */
int ui_run(void);

/* Calls fn(ctx) every ms milliseconds on the UI thread (0 or fn 0 stops it). UI thread only. */
void ui_set_tick(ui_tick_fn fn, void *ctx, uint32_t ms);

/* --- Any thread ---------------------------------------------------------------------- */

/* Launcher state: drives visible_when, status texts and whether play is enabled. Clears a
 * status set with ui_set_status. */
void ui_set_state(theme_launcher_state state);

/* Download progress for progress bars and {percent} {done} {total}. */
void ui_set_progress(uint64_t done, uint64_t total);

/* Replaces the status.<state> text of status elements until the next ui_set_state: a string
 * key of the theme, or a literal shown as is. 0 or "" goes back to status.<state>. */
void ui_set_status(const char *key_or_text);

/* Seconds until the server opens, as computed from the server's clock (opens_at -
 * server_time). The window counts down with the monotonic tick count, never the PC clock.
 * Negative = no date (countdown shows 0). */
void ui_set_countdown(int64_t seconds);

/* News items for news elements (copied, at most STATUS_NEWS_MAX). Their images are loaded
 * with load_blob. */
void ui_set_news(const status_news *items, uint32_t count);

/* Replaces the theme in place (size, title, region, images, fonts). The app calls it once
 * every blob of theme_required_blobs is in the cache. */
void ui_set_theme(const theme_model *theme);

/* {version} in texts. */
void ui_set_version(const char *version);

/* Closes the window (ui_run then returns) without reporting EXIT. */
void ui_close(void);

#endif
