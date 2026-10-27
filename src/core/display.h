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

/* Whether a button with this action reacts in launcher state ls: play only in ready, the
 * others always. */
int display_action_enabled(theme_action action, theme_launcher_state ls);

#endif
