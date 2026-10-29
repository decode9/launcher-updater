#include "testlib.h"
#include "../src/core/display.h"

TEST(sizes)
{
    char out[32];
    display_size(0, out, sizeof out);
    CHECK_STR(out, "0 B");
    display_size(1023, out, sizeof out);
    CHECK_STR(out, "1023 B");
    display_size(1024, out, sizeof out);
    CHECK_STR(out, "1.0 KB");
    display_size(12 * 1048576ull + 524288, out, sizeof out);
    CHECK_STR(out, "12.5 MB");
    display_size(3435973837ull, out, sizeof out); /* 3.2 GiB */
    CHECK_STR(out, "3.2 GB");
    display_size(1ull << 40, out, sizeof out);
    CHECK_STR(out, "1.0 TB");
    display_size(5000ull << 40, out, sizeof out);
    CHECK_STR(out, "5000.0 TB");
    display_size(1048576, out, 4); /* truncated, still terminated */
    CHECK_STR(out, "1.0");
}

TEST(percent)
{
    CHECK_EQ_U64(display_percent(0, 0), 0);
    CHECK_EQ_U64(display_percent(5, 0), 0);
    CHECK_EQ_U64(display_percent(1, 2), 50);
    CHECK_EQ_U64(display_percent(3, 2), 100);
    CHECK_EQ_U64(display_percent(999, 1000), 99);
    CHECK_EQ_U64(display_percent(1ull << 40, 1ull << 41), 50);
    CHECK_EQ_U64(display_percent(0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull), 100);
}

TEST(fill_width)
{
    CHECK_EQ_U64(display_fill_width(0, 100, 560), 0);
    CHECK_EQ_U64(display_fill_width(50, 100, 560), 280);
    CHECK_EQ_U64(display_fill_width(100, 100, 560), 560);
    CHECK_EQ_U64(display_fill_width(200, 100, 560), 560);
    CHECK_EQ_U64(display_fill_width(1ull << 50, 1ull << 51, 4096), 2048);
    CHECK_EQ_U64(display_fill_width(1, 0, 560), 0);
}

TEST(countdown)
{
    display_countdown c;
    display_countdown_split(0, &c);
    CHECK_STR(c.days, "0");
    CHECK_STR(c.hours, "00");
    CHECK_STR(c.minutes, "00");
    CHECK_STR(c.seconds, "00");
    display_countdown_split(2 * 86400 + 3 * 3600 + 4 * 60 + 5, &c);
    CHECK_STR(c.days, "2");
    CHECK_STR(c.hours, "03");
    CHECK_STR(c.minutes, "04");
    CHECK_STR(c.seconds, "05");
    display_countdown_split(0xFFFFFFFFu, &c);
    CHECK_STR(c.days, "49710");
    CHECK_STR(c.hours, "06");
    CHECK_STR(c.minutes, "28");
    CHECK_STR(c.seconds, "15");
}

TEST(actions)
{
    CHECK(display_action_enabled(THEME_ACT_PLAY, THEME_LS_READY, 1));
    CHECK(!display_action_enabled(THEME_ACT_PLAY, THEME_LS_CHECKING, 1));
    CHECK(!display_action_enabled(THEME_ACT_PLAY, THEME_LS_LOCKED, 1));
    CHECK(!display_action_enabled(THEME_ACT_PLAY, THEME_LS_INSTALLING, 1));
    CHECK(!display_action_enabled(THEME_ACT_PLAY, THEME_LS_ERROR, 1));
    CHECK(display_action_enabled(THEME_ACT_EXIT, THEME_LS_ERROR, 1));
    CHECK(display_action_enabled(THEME_ACT_MINIMIZE, THEME_LS_INSTALLING, 1));
    CHECK(display_action_enabled(THEME_ACT_OPEN_URL, THEME_LS_CHECKING, 1));
    CHECK(!display_action_enabled(THEME_ACT_PLAY, THEME_LS_READY, 0)); /* nothing to play */
    CHECK(display_action_enabled(THEME_ACT_EXIT, THEME_LS_READY, 0));
    CHECK(display_action_enabled(THEME_ACT_OPEN_URL, THEME_LS_READY, 0));
}

TEST(dpi_scale)
{
    CHECK(display_scale_from_dpi(0) == 100);
    CHECK(display_scale_from_dpi(72) == 100);
    CHECK(display_scale_from_dpi(96) == 100);
    CHECK(display_scale_from_dpi(120) == 125);
    CHECK(display_scale_from_dpi(144) == 150);
    CHECK(display_scale_from_dpi(168) == 175);
    CHECK(display_scale_from_dpi(192) == 200);
    CHECK(display_scale_from_dpi(100) == 104);
    CHECK(display_scale_from_dpi(480) == 500);
    CHECK(display_scale_from_dpi(100000) == DISPLAY_SCALE_MAX);
}

TEST(dpi_fit)
{
    /* fits: unchanged */
    CHECK(display_scale_fit(150, 960, 600, 1920, 1040) == 150);
    CHECK(display_scale_fit(200, 960, 600, 1920, 1200) == 200);
    /* too wide / too tall: the largest scale that fits */
    CHECK(display_scale_fit(200, 960, 600, 1440, 1000) == 150);
    CHECK(display_scale_fit(200, 960, 600, 3000, 1000) == 166);
    CHECK(display_scaled(600, 166) <= 1000);
    /* below 100 when even 1:1 does not fit, down to the minimum */
    CHECK(display_scale_fit(100, 4096, 4096, 1920, 1040) == 25);
    CHECK(display_scale_fit(100, 2000, 1000, 1920, 1040) == 96);
    CHECK(display_scaled(2000, 96) <= 1920);
    /* unknown work area */
    CHECK(display_scale_fit(150, 960, 600, 0, 0) == 150);
}

TEST(dpi_points)
{
    CHECK(display_scaled(960, 100) == 960);
    CHECK(display_scaled(960, 150) == 1440);
    CHECK(display_scaled(14, 125) == 18); /* 17.5 rounds up */
    CHECK(display_scaled(4096, 500) == 20480);
    CHECK(display_scaled(0, 150) == 0);
    CHECK(display_unscaled(0, 150) == 0);
    CHECK(display_unscaled(149, 150) == 99);
    CHECK(display_unscaled(150, 150) == 100);
    CHECK(display_unscaled(1439, 150) == 959);
    CHECK(display_unscaled(-1, 150) == -1);
    CHECK(display_unscaled(-1, 50) == -2);
    CHECK(display_unscaled(10, 0) == 10);
    CHECK(display_unscaled(399, 200) == 199);
}

static status_news news_with(const char *category)
{
    status_news n;
    memset(&n, 0, sizeof n);
    strcpy(n.category, category);
    return n;
}

TEST(news_filter)
{
    status_news items[5];
    uint8_t idx[STATUS_NEWS_MAX];
    items[0] = news_with("updates");
    items[1] = news_with("events");
    items[2] = news_with("");
    items[3] = news_with("updates");
    items[4] = news_with("update");
    memset(idx, 0xEE, sizeof idx);
    CHECK_EQ_U64(display_news_filter(items, 5, "updates", idx, STATUS_NEWS_MAX), 2);
    CHECK_EQ_U64(idx[0], 0);
    CHECK_EQ_U64(idx[1], 3);
    CHECK_EQ_U64(idx[2], 0xEE);                                               /* nothing more written */
    CHECK_EQ_U64(display_news_filter(items, 5, "events", idx, STATUS_NEWS_MAX), 1);
    CHECK_EQ_U64(idx[0], 1);
    CHECK_EQ_U64(display_news_filter(items, 5, "", idx, STATUS_NEWS_MAX), 5); /* all, in order */
    CHECK(idx[0] == 0 && idx[1] == 1 && idx[2] == 2 && idx[3] == 3 && idx[4] == 4);
    CHECK_EQ_U64(display_news_filter(items, 5, 0, idx, STATUS_NEWS_MAX), 5);
    CHECK_EQ_U64(display_news_filter(items, 5, "Updates", idx, STATUS_NEWS_MAX), 0); /* exact */
    CHECK_EQ_U64(display_news_filter(items, 5, "none", idx, STATUS_NEWS_MAX), 0);
    CHECK_EQ_U64(display_news_filter(items, 0, 0, idx, STATUS_NEWS_MAX), 0);
    memset(idx, 0xEE, sizeof idx);
    CHECK_EQ_U64(display_news_filter(items, 5, 0, idx, 2), 2);               /* cap respected */
    CHECK(idx[0] == 0 && idx[1] == 1 && idx[2] == 0xEE);
    CHECK_EQ_U64(display_news_filter(items, 5, 0, 0, 0), 0);
}

TEST(news_date)
{
    char out[32];
    display_news_date("2026-10-28", "{mm}-{dd}", out, sizeof out);
    CHECK_STR(out, "10-28");
    display_news_date("2026-10-28", "{dd}.{mm}.{yyyy}", out, sizeof out);
    CHECK_STR(out, "28.10.2026");
    display_news_date("2026-01-05", "[{yyyy}/{mm}/{dd}] {x} {", out, sizeof out);
    CHECK_STR(out, "[2026/01/05] {x} {");                                     /* unknown fields kept */
    display_news_date("2026-10-28", "", out, sizeof out);
    CHECK_STR(out, "");
    display_news_date("2026-10-28", 0, out, sizeof out);
    CHECK_STR(out, "2026-10-28");
    display_news_date("", "{mm}-{dd}", out, sizeof out);
    CHECK_STR(out, "");                                                        /* no date */
    display_news_date(0, "{mm}-{dd}", out, sizeof out);
    CHECK_STR(out, "");
    display_news_date("2026-02-30", "{mm}-{dd}", out, sizeof out);
    CHECK_STR(out, "");                                                        /* invalid date */
    display_news_date("2026-10-28", "{dd}.{mm}.{yyyy}", out, 6);
    CHECK_STR(out, "28.10");                                                   /* cut, still terminated */
}

TEST(menu_entries)
{
    CHECK(display_repair_enabled(THEME_LS_READY));
    CHECK(display_repair_enabled(THEME_LS_ERROR));
    CHECK(!display_repair_enabled(THEME_LS_CHECKING));
    CHECK(!display_repair_enabled(THEME_LS_LOCKED));
    CHECK(!display_repair_enabled(THEME_LS_INSTALLING));
    CHECK(display_settings_enabled(THEME_LS_READY));
    CHECK(display_settings_enabled(THEME_LS_LOCKED));
    CHECK(display_settings_enabled(THEME_LS_ERROR));
    CHECK(!display_settings_enabled(THEME_LS_CHECKING));
    CHECK(!display_settings_enabled(THEME_LS_INSTALLING));
}

int main(void)
{
    RUN(sizes);
    RUN(percent);
    RUN(fill_width);
    RUN(countdown);
    RUN(actions);
    RUN(dpi_scale);
    RUN(dpi_fit);
    RUN(dpi_points);
    RUN(news_filter);
    RUN(news_date);
    RUN(menu_entries);
    return test_report();
}
