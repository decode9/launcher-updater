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
    CHECK(display_action_enabled(THEME_ACT_PLAY, THEME_LS_READY));
    CHECK(!display_action_enabled(THEME_ACT_PLAY, THEME_LS_CHECKING));
    CHECK(!display_action_enabled(THEME_ACT_PLAY, THEME_LS_LOCKED));
    CHECK(!display_action_enabled(THEME_ACT_PLAY, THEME_LS_INSTALLING));
    CHECK(!display_action_enabled(THEME_ACT_PLAY, THEME_LS_ERROR));
    CHECK(display_action_enabled(THEME_ACT_EXIT, THEME_LS_ERROR));
    CHECK(display_action_enabled(THEME_ACT_MINIMIZE, THEME_LS_INSTALLING));
    CHECK(display_action_enabled(THEME_ACT_OPEN_URL, THEME_LS_CHECKING));
}

int main(void)
{
    RUN(sizes);
    RUN(percent);
    RUN(fill_width);
    RUN(countdown);
    RUN(actions);
    return test_report();
}
