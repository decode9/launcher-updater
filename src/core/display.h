/*
 * display.h - Pure helpers that turn launcher numbers into what a theme shows: human sizes for
 * {done}/{total}, the percentage, the countdown fields and which actions are enabled in which
 * launcher state (docs/theming.md). No 64-bit division (the Windows build has no CRT helpers
 * for it): everything is shifts and 32-bit arithmetic.
 */
#ifndef LAUNCHER_CORE_DISPLAY_H
#define LAUNCHER_CORE_DISPLAY_H

#include <stddef.h>
#include <stdint.h>

#include "status.h"
#include "theme.h"

/* "0 B", "999 B", "1.0 KB", "12.5 MB", "3.2 GB", "1.0 TB" (1024-based, one decimal,
 * truncated). Always NUL-terminated when cap > 0. */
void display_size(uint64_t bytes, char *out, size_t cap);

/* done / total as 0..100 (truncated). total 0 gives 0; done beyond total gives 100. */
uint32_t display_percent(uint64_t done, uint64_t total);

/* Pixels of a bar of width px that are done (0..width). */
uint32_t display_fill_width(uint64_t done, uint64_t total, uint32_t width);

/* Countdown fields: days without padding, hours/minutes/seconds always two digits. */
typedef struct {
    char days[12];
    char hours[3], minutes[3], seconds[3];
} display_countdown;

void display_countdown_split(uint32_t seconds, display_countdown *out);

/* Whether a button with this action reacts in launcher state ls: play only in ready and while
 * play_enabled (0 when there is nothing to play), the others always. */
int display_action_enabled(theme_action action, theme_launcher_state ls, int play_enabled);

/* Options menu entries: "Verify and repair files" only on the ready or error screen (as
 * flow_on_repair accepts it); "Game settings" whenever no install or check is running (ready,
 * locked, error), since the settings program belongs to the installed files. */
int display_repair_enabled(theme_launcher_state ls);
int display_settings_enabled(theme_launcher_state ls);

/* ---- news ---------------------------------------------------------------- */

/* Writes, in order, the indices of the items whose category equals category exactly (0 or ""
 * selects every item, also those without a category) into out_index, at most cap of them.
 * Returns how many it wrote. Indices fit in a byte because count <= STATUS_NEWS_MAX. */
uint32_t display_news_filter(const status_news *items, uint32_t count, const char *category, uint8_t *out_index,
                             uint32_t cap);

/* Expands a news date ("YYYY-MM-DD", see status_news.date) through a template: {yyyy}, {mm}
 * and {dd} become the year and the two-digit month and day ("{dd}.{mm}" -> "28.10"); any other
 * text, including unknown {fields}, is copied as is. An empty or invalid date gives "", a null
 * template gives the date unchanged. Always NUL-terminated when cap > 0; cut before the first piece that does not fit.
 * (A small expander of its own: theme_format's variables are not touched for this.) */
void display_news_date(const char *date, const char *tmpl, char *out, size_t cap);

/* ---- DPI scaling: themes are in logical pixels (96 DPI); the window shows them at a scale in
 * percent (100 = 1:1, 150 at 144 DPI...). */
#define DISPLAY_SCALE_MIN 25  /* lowest scale display_scale_fit goes to */
#define DISPLAY_SCALE_MAX 500 /* highest scale display_scale_from_dpi gives */

/* Scale for a system DPI, rounded to the nearest percent: 96 -> 100, 120 -> 125, 144 -> 150,
 * 192 -> 200. 0 or anything below 96 gives 100; capped at DISPLAY_SCALE_MAX. */
uint32_t display_scale_from_dpi(uint32_t dpi);

/* The largest scale <= scale at which a w x h window fits in an area_w x area_h work area (it
 * may go below 100, so a theme bigger than the screen still shows whole), never below
 * DISPLAY_SCALE_MIN. An empty area (0) leaves scale as is. */
uint32_t display_scale_fit(uint32_t scale, uint32_t w, uint32_t h, uint32_t area_w, uint32_t area_h);

/* Logical -> device pixels, rounded to nearest (v up to 65535). */
int display_scaled(int v, uint32_t scale);

/* Device -> logical pixels, rounded down (also for negative v, so a point left of or above the
 * window never lands on column or row 0). scale 0 counts as 100. */
int display_unscaled(int v, uint32_t scale);

#endif
