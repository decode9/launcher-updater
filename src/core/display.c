/*
 * display.c - See display.h.
 */
#include "display.h"
#include "str.h"

/* Clamps *done to total, then halves both until total fits in `bits` bits, so the caller's
 * 32-bit product (done * 100, done * width) cannot overflow. Returns the scaled total. */
static uint32_t scale(uint64_t *done, uint64_t total, unsigned bits)
{
    if (*done > total) *done = total;
    while (total >> bits) {
        total >>= 1;
        *done >>= 1;
    }
    return (uint32_t)total;
}

uint32_t display_percent(uint64_t done, uint64_t total)
{
    uint32_t t;
    if (!total) return 0;
    t = scale(&done, total, 24);
    if (!t) return 0;
    return (uint32_t)done * 100u / t;
}

uint32_t display_fill_width(uint64_t done, uint64_t total, uint32_t width)
{
    uint32_t t;
    if (!total) return 0;
    if (width > 4096) width = 4096;
    t = scale(&done, total, 19); /* 2^19 * 4096 = 2^31 */
    if (!t) return 0;
    return (uint32_t)done * width / t;
}

void display_size(uint64_t bytes, char *out, size_t cap)
{
    static const char *const units[] = {"KB", "MB", "GB", "TB"};
    strbuf sb;
    unsigned unit = 0, shift = 10;
    sb_init(&sb, out, cap);
    if (bytes < 1024) {
        sb_add_u64(&sb, bytes);
        sb_add(&sb, " B");
        return;
    }
    while (unit < 3 && (bytes >> (shift + 10)) != 0) {
        unit++;
        shift += 10;
    }
    sb_add_u64(&sb, bytes >> shift);
    sb_add_char(&sb, '.');
    /* First decimal: (remainder * 10) >> shift; the remainder is below 2^40, so no overflow. */
    sb_add_char(&sb, (char)('0' + (((bytes & ((1ull << shift) - 1)) * 10) >> shift)));
    sb_add_char(&sb, ' ');
    sb_add(&sb, units[unit]);
}

static void two(char out[3], uint32_t v)
{
    out[0] = (char)('0' + v / 10);
    out[1] = (char)('0' + v % 10);
    out[2] = 0;
}

void display_countdown_split(uint32_t seconds, display_countdown *out)
{
    uint32_t days = seconds / 86400, rest = seconds % 86400;
    u64_to_dec(days, out->days);
    two(out->hours, rest / 3600);
    two(out->minutes, rest % 3600 / 60);
    two(out->seconds, rest % 60);
}

int display_action_enabled(theme_action action, theme_launcher_state ls, int play_enabled)
{
    return action != THEME_ACT_PLAY || (ls == THEME_LS_READY && play_enabled);
}

uint32_t display_scale_from_dpi(uint32_t dpi)
{
    uint32_t pct;
    if (dpi <= 96) return 100;
    if (dpi > 96 * DISPLAY_SCALE_MAX / 100) return DISPLAY_SCALE_MAX;
    pct = (dpi * 100 + 48) / 96;
    return pct;
}

uint32_t display_scale_fit(uint32_t scale, uint32_t w, uint32_t h, uint32_t area_w, uint32_t area_h)
{
    /* scale * w <= area_w * 100 keeps the rounded scaled size within area_w (see display_scaled). */
    if (area_w && w && area_w < 0x00FFFFFFu && scale * w > area_w * 100) scale = area_w * 100 / w;
    if (area_h && h && area_h < 0x00FFFFFFu && scale * h > area_h * 100) scale = area_h * 100 / h;
    return scale < DISPLAY_SCALE_MIN ? DISPLAY_SCALE_MIN : scale;
}

int display_scaled(int v, uint32_t scale)
{
    if (v <= 0) return v;
    return (int)(((uint32_t)v * scale + 50) / 100);
}

int display_unscaled(int v, uint32_t scale)
{
    if (!scale) scale = 100;
    if (v >= 0) return (int)((uint32_t)v * 100 / scale);
    return -(int)(((uint32_t)-v * 100 + scale - 1) / scale);
}
