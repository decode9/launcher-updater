#include "testlib.h"
#include "../src/core/flow.h"

/* ---- helpers ------------------------------------------------------------------------ */

static flow_facts open_facts(int current)
{
    flow_facts x = {1, 1, 1, 1, -1, current, 0};
    return x;
}

static flow_facts locked_facts(int64_t secs)
{
    flow_facts x = {1, 1, 0, 1, secs, 0, 0};
    return x;
}

static flow_facts no_server(int cached_open, int current)
{
    flow_facts x = {0, 1, cached_open, 1, -1, current, 0};
    return x;
}

static int key_is(const flow_step *s, const char *key)
{
    if (!key) return s->view.key == 0;
    return s->view.key && strcmp(s->view.key, key) == 0 && s->view.fallback && s->view.fallback[0];
}

static int waits_on(const flow_step *s, theme_launcher_state screen, const char *key, uint32_t ms)
{
    return s->action == FLOW_DO_WAIT && s->show && s->view.screen == screen && key_is(s, key) && s->wait_ms == ms;
}

/* Starts and answers the first status. */
static flow_step first(flow_state *f, flow_facts facts)
{
    flow_start(f);
    return flow_on_status(f, &facts, 0);
}

/* ---- behaviour kept from 0.1 -------------------------------------------------------- */

TEST(start_fetches_while_checking)
{
    flow_state f;
    flow_step s = flow_start(&f);
    CHECK(s.action == FLOW_DO_FETCH && s.show && s.view.screen == THEME_LS_CHECKING && !s.view.play_enabled);
}

TEST(ready_texts)
{
    flow_state f;
    flow_facts x = open_facts(1);
    flow_step s = first(&f, x);
    CHECK(waits_on(&s, THEME_LS_READY, 0, FLOW_RECHECK_MS) && s.view.play_enabled);
    x.current = 0;
    s = first(&f, x);
    CHECK(waits_on(&s, THEME_LS_READY, "status.update", FLOW_RECHECK_MS) && s.view.play_enabled);
    x.interrupted = 1;
    s = first(&f, x);
    CHECK(waits_on(&s, THEME_LS_READY, "status.resume", FLOW_RECHECK_MS) && s.view.play_enabled);
}

TEST(timeout_polls_without_changing_the_screen)
{
    flow_state f;
    flow_step s;
    first(&f, open_facts(1));
    s = flow_on_timeout(&f);
    CHECK(s.action == FLOW_DO_FETCH && !s.show);
}

TEST(locked_waits)
{
    flow_state f;
    flow_step s = first(&f, locked_facts(3600));
    CHECK(waits_on(&s, THEME_LS_LOCKED, 0, FLOW_RECHECK_MS) && !s.view.play_enabled);
    CHECK_EQ_U64(s.view.countdown, 3600);
    s = first(&f, locked_facts(300)); /* not below the recheck period */
    CHECK_EQ_U64(s.wait_ms, FLOW_RECHECK_MS);
    s = first(&f, locked_facts(299)); /* countdown < 5 min: wake when it ends */
    CHECK_EQ_U64(s.wait_ms, 300000);
    s = first(&f, locked_facts(10));
    CHECK_EQ_U64(s.wait_ms, 11000);
    s = first(&f, locked_facts(4));  /* never below 5 s */
    CHECK_EQ_U64(s.wait_ms, 5000);
    s = first(&f, locked_facts(1));
    CHECK_EQ_U64(s.wait_ms, 5000);
    s = first(&f, locked_facts(-1)); /* no date: every recheck period, as before */
    CHECK(waits_on(&s, THEME_LS_LOCKED, 0, FLOW_RECHECK_MS));
    CHECK(s.view.countdown == -1);
}

TEST(offline_or_error)
{
    flow_state f;
    flow_facts x = no_server(1, 1);
    flow_step s = first(&f, x);
    CHECK(waits_on(&s, THEME_LS_READY, "status.offline", FLOW_RETRY_MS) && s.view.play_enabled);
    x = no_server(0, 1); /* the last status said closed */
    s = first(&f, x);
    CHECK(waits_on(&s, THEME_LS_ERROR, "status.connection", FLOW_RETRY_MS) && !s.view.play_enabled);
    x = no_server(1, 0); /* not installed */
    s = first(&f, x);
    CHECK(waits_on(&s, THEME_LS_ERROR, "status.connection", FLOW_RETRY_MS));
    x.known = 0; /* no cached status */
    x.current = 1;
    s = first(&f, x);
    CHECK(waits_on(&s, THEME_LS_ERROR, "status.connection", FLOW_RETRY_MS));
}

TEST(play_installs_then_launches)
{
    flow_state f;
    flow_facts x = open_facts(0);
    flow_step s;
    first(&f, x);
    flow_on_play(&f);
    s = flow_on_status(&f, &x, 0);
    CHECK(s.action == FLOW_DO_INSTALL && s.show && s.view.screen == THEME_LS_INSTALLING && !s.view.play_enabled);
    s = flow_on_install(&f, FLOW_INSTALL_OK);
    CHECK(s.action == FLOW_DO_LAUNCH);
    s = flow_on_launch(&f, 1);
    CHECK(s.action == FLOW_DO_CLOSE);
}

TEST(play_launches_when_current)
{
    flow_state f;
    flow_facts x = open_facts(1);
    flow_step s;
    first(&f, x);
    flow_on_play(&f);
    s = flow_on_status(&f, &x, 0);
    CHECK(s.action == FLOW_DO_LAUNCH);
}

TEST(install_and_launch_failures)
{
    flow_state f;
    flow_facts x = open_facts(0);
    flow_step s;
    first(&f, x);
    flow_on_play(&f);
    flow_on_status(&f, &x, 0);
    s = flow_on_install(&f, FLOW_INSTALL_FAILED);
    CHECK(waits_on(&s, THEME_LS_READY, "status.install_failed", FLOW_RECHECK_MS) && s.view.play_enabled);
    flow_on_play(&f);
    flow_on_status(&f, &x, 0);
    s = flow_on_install(&f, FLOW_INSTALL_NO_SPACE);
    CHECK(waits_on(&s, THEME_LS_READY, "status.no_space", FLOW_RECHECK_MS) && s.view.play_enabled);
    x.current = 1;
    flow_on_play(&f);
    flow_on_status(&f, &x, 0);
    s = flow_on_launch(&f, 0);
    CHECK(waits_on(&s, THEME_LS_READY, "status.launch_failed", FLOW_RECHECK_MS) && s.view.play_enabled);
    /* the error stays until the next poll, which shows the normal ready text */
    flow_on_timeout(&f);
    s = flow_on_status(&f, &x, 0);
    CHECK(waits_on(&s, THEME_LS_READY, 0, FLOW_RECHECK_MS));
}

TEST(play_ignored_when_disabled)
{
    flow_state f;
    flow_step s;
    first(&f, locked_facts(3600));
    s = flow_on_play(&f);
    CHECK(s.action == FLOW_DO_NONE);
    first(&f, no_server(0, 0));
    s = flow_on_play(&f);
    CHECK(s.action == FLOW_DO_NONE);
}

TEST(play_ignored_while_installing)
{
    flow_state f;
    flow_facts x = open_facts(0);
    flow_step s;
    first(&f, x);
    flow_on_play(&f);
    flow_on_status(&f, &x, 0);
    s = flow_on_play(&f);
    CHECK(s.action == FLOW_DO_NONE);
    s = flow_on_install(&f, FLOW_INSTALL_OK);
    CHECK(s.action == FLOW_DO_LAUNCH);
}

/* ---- H2: a click during a poll is not lost ----------------------------------------- */

TEST(h2_click_during_poll_is_served)
{
    flow_state f;
    flow_facts x = open_facts(0);
    flow_step s;
    first(&f, x);
    flow_on_timeout(&f);                 /* background poll in progress */
    s = flow_on_play(&f);                /* the click arrives meanwhile */
    CHECK(s.action == FLOW_DO_NONE);
    s = flow_on_status(&f, &x, 0);       /* that poll is fresh: no second fetch */
    CHECK(s.action == FLOW_DO_INSTALL);
}

TEST(h2_click_during_first_fetch)
{
    flow_state f;
    flow_facts x = open_facts(1);
    flow_step s;
    flow_start(&f);
    flow_on_play(&f);
    s = flow_on_status(&f, &x, 0);
    CHECK(s.action == FLOW_DO_LAUNCH);
}

TEST(h2_pending_click_dropped_when_not_playable)
{
    flow_state f;
    flow_facts x = open_facts(1);
    flow_step s;
    first(&f, x);
    flow_on_timeout(&f);
    flow_on_play(&f);
    x = locked_facts(3600);              /* closed meanwhile */
    s = flow_on_status(&f, &x, 0);
    CHECK(waits_on(&s, THEME_LS_LOCKED, 0, FLOW_RECHECK_MS));
    x = open_facts(1);                   /* reopens: the old click does not start the game */
    flow_on_timeout(&f);
    s = flow_on_status(&f, &x, 0);
    CHECK(waits_on(&s, THEME_LS_READY, 0, FLOW_RECHECK_MS));
}

/* ---- H3: Play decides with a fresh status ------------------------------------------- */

TEST(h3_play_fetches_first)
{
    flow_state f;
    flow_step s;
    first(&f, open_facts(0));
    s = flow_on_play(&f);
    CHECK(s.action == FLOW_DO_FETCH && s.show && s.view.screen == THEME_LS_CHECKING && !s.view.play_enabled);
}

TEST(h3_fresh_status_decides)
{
    flow_state f;
    flow_facts x;
    flow_step s;
    first(&f, open_facts(0));            /* old status: update needed */
    flow_on_play(&f);
    x = open_facts(1);                   /* fresh: already current */
    s = flow_on_status(&f, &x, 0);
    CHECK(s.action == FLOW_DO_LAUNCH);

    first(&f, open_facts(1));
    flow_on_play(&f);
    x = open_facts(0);                   /* a new release came out */
    s = flow_on_status(&f, &x, 0);
    CHECK(s.action == FLOW_DO_INSTALL);

    first(&f, open_facts(1));
    flow_on_play(&f);
    x = locked_facts(600);               /* now closed */
    s = flow_on_status(&f, &x, 0);
    CHECK(waits_on(&s, THEME_LS_LOCKED, 0, FLOW_RECHECK_MS));

    first(&f, open_facts(1));
    flow_on_play(&f);
    x = open_facts(1);
    x.has_release = 0;                   /* the release was withdrawn */
    s = flow_on_status(&f, &x, 0);
    CHECK(waits_on(&s, THEME_LS_READY, "status.no_release", FLOW_RECHECK_MS) && !s.view.play_enabled);
}

TEST(h3_retry_after_install_failure_fetches_again)
{
    flow_state f;
    flow_facts x = open_facts(0);
    flow_step s;
    first(&f, x);
    flow_on_play(&f);
    flow_on_status(&f, &x, 0);
    flow_on_install(&f, FLOW_INSTALL_FAILED);
    s = flow_on_play(&f);
    CHECK(s.action == FLOW_DO_FETCH);
}

TEST(h3_no_server_on_play)
{
    flow_state f;
    flow_facts x;
    flow_step s;
    first(&f, open_facts(1));
    flow_on_play(&f);
    x = no_server(1, 1);                 /* offline allowed: play the installed release */
    s = flow_on_status(&f, &x, 0);
    CHECK(s.action == FLOW_DO_LAUNCH);

    first(&f, open_facts(0));
    flow_on_play(&f);
    x = no_server(1, 0);                 /* nothing playable offline */
    s = flow_on_status(&f, &x, 0);
    CHECK(waits_on(&s, THEME_LS_ERROR, "status.connection", FLOW_RETRY_MS));

    first(&f, no_server(1, 1));          /* offline ready: Play still asks the server first */
    s = flow_on_play(&f);
    CHECK(s.action == FLOW_DO_FETCH);
    x = open_facts(0);                   /* back online with a new release */
    s = flow_on_status(&f, &x, 0);
    CHECK(s.action == FLOW_DO_INSTALL);
}

/* ---- H8: open without a release ----------------------------------------------------- */

TEST(h8_no_release)
{
    flow_state f;
    flow_facts x = open_facts(0);
    flow_step s;
    x.has_release = 0;
    s = first(&f, x);
    CHECK(waits_on(&s, THEME_LS_READY, "status.no_release", FLOW_RECHECK_MS));
    CHECK(!s.view.play_enabled);
    CHECK_STR(s.view.fallback, "No version has been published yet.");
    s = flow_on_play(&f);
    CHECK(s.action == FLOW_DO_NONE);
    x.interrupted = 1;
    s = first(&f, x);
    CHECK(key_is(&s, "status.no_release"));
}

TEST(h8_no_release_offline_is_an_error)
{
    flow_state f;
    flow_facts x = no_server(1, 0);
    flow_step s;
    x.has_release = 0;
    s = first(&f, x);
    CHECK(waits_on(&s, THEME_LS_ERROR, "status.connection", FLOW_RETRY_MS));
}

/* ---- H11: due countdown backs off with jitter --------------------------------------- */

TEST(h11_due_backoff)
{
    static const uint32_t expected[] = {5000, 10000, 20000, 40000, 60000, 60000, 60000};
    flow_state f;
    flow_facts x = locked_facts(0);
    flow_step s;
    uint32_t i;
    flow_start(&f);
    for (i = 0; i < sizeof expected / sizeof expected[0]; i++) {
        s = flow_on_status(&f, &x, 0);
        CHECK(waits_on(&s, THEME_LS_LOCKED, 0, expected[i]));
        CHECK(s.view.countdown == 0);
        flow_on_timeout(&f);
    }
}

TEST(h11_jitter_bounds)
{
    flow_state f;
    flow_facts x = locked_facts(0);
    flow_step s;
    s = first(&f, x);                    /* random 0: no jitter */
    CHECK_EQ_U64(s.wait_ms, 5000);
    flow_start(&f);
    s = flow_on_status(&f, &x, 0xFFFFFFFFu);
    CHECK(s.wait_ms > 5000 && s.wait_ms <= 6250);
    flow_start(&f);
    s = flow_on_status(&f, &x, 0x8000u);  /* half of the 25 % */
    CHECK_EQ_U64(s.wait_ms, 5625);
    flow_start(&f);
    for (int i = 0; i < 6; i++) s = flow_on_status(&f, &x, 0xFFFFu);
    CHECK(s.wait_ms > 60000 && s.wait_ms < 75000);
}

TEST(h11_series_restarts_when_state_changes)
{
    flow_state f;
    flow_facts due = locked_facts(0), x;
    flow_step s;
    flow_start(&f);
    flow_on_status(&f, &due, 0);
    flow_on_status(&f, &due, 0);
    s = flow_on_status(&f, &due, 0);
    CHECK_EQ_U64(s.wait_ms, 20000);
    x = locked_facts(120);               /* opening postponed */
    s = flow_on_status(&f, &x, 0);
    CHECK_EQ_U64(s.wait_ms, 121000);
    s = flow_on_status(&f, &due, 0);
    CHECK_EQ_U64(s.wait_ms, 5000);
    flow_on_status(&f, &due, 0);
    x = no_server(0, 0);                 /* connection lost */
    flow_on_status(&f, &x, 0);
    s = flow_on_status(&f, &due, 0);
    CHECK_EQ_U64(s.wait_ms, 5000);
    flow_on_status(&f, &due, 0);
    x = open_facts(1);                   /* opened */
    flow_on_status(&f, &x, 0);
    s = flow_on_status(&f, &due, 0);
    CHECK_EQ_U64(s.wait_ms, 5000);
}

/* ---- repair (Options -> repair) ---------------------------------------------------- */

TEST(repair_from_ready_verifies_and_does_not_launch)
{
    flow_state f;
    flow_facts x = open_facts(1);
    flow_step s;
    first(&f, x);
    s = flow_on_repair(&f);
    CHECK(s.action == FLOW_DO_FETCH && s.show && s.view.screen == THEME_LS_CHECKING);
    s = flow_on_status(&f, &x, 0);
    CHECK(s.action == FLOW_DO_INSTALL && s.repair && s.show && s.view.screen == THEME_LS_INSTALLING);
    CHECK(key_is(&s, "status.repairing") && !s.view.play_enabled);
    s = flow_on_play(&f); /* ignored while repairing */
    CHECK(s.action == FLOW_DO_NONE);
    s = flow_on_install(&f, FLOW_INSTALL_OK);
    CHECK(s.action == FLOW_DO_FETCH && s.show && s.view.screen == THEME_LS_CHECKING);
    s = flow_on_status(&f, &x, 0);
    CHECK(waits_on(&s, THEME_LS_READY, 0, FLOW_RECHECK_MS) && s.view.play_enabled && !s.repair);
    /* the next Play is a normal one: it launches */
    flow_on_play(&f);
    s = flow_on_status(&f, &x, 0);
    CHECK(s.action == FLOW_DO_LAUNCH);
}

TEST(repair_failure_shows_error_then_recovers)
{
    flow_state f;
    flow_facts x = open_facts(1);
    flow_step s;
    first(&f, x);
    flow_on_repair(&f);
    flow_on_status(&f, &x, 0);
    s = flow_on_install(&f, FLOW_INSTALL_FAILED);
    CHECK(waits_on(&s, THEME_LS_ERROR, "status.repair_failed", FLOW_RETRY_MS) && !s.view.play_enabled);
    /* allowed again from the error screen */
    s = flow_on_repair(&f);
    CHECK(s.action == FLOW_DO_FETCH);
    s = flow_on_status(&f, &x, 0);
    CHECK(s.action == FLOW_DO_INSTALL && s.repair);
    s = flow_on_install(&f, FLOW_INSTALL_NO_SPACE);
    CHECK(waits_on(&s, THEME_LS_ERROR, "status.no_space", FLOW_RETRY_MS));
    /* the poll puts the usual screen back */
    flow_on_timeout(&f);
    s = flow_on_status(&f, &x, 0);
    CHECK(waits_on(&s, THEME_LS_READY, 0, FLOW_RECHECK_MS) && s.view.play_enabled);
}

TEST(repair_ignored_outside_ready_and_error)
{
    flow_state f;
    flow_facts x = open_facts(0);
    flow_step s;
    first(&f, locked_facts(3600));
    s = flow_on_repair(&f);
    CHECK(s.action == FLOW_DO_NONE);
    flow_start(&f); /* checking */
    s = flow_on_repair(&f);
    CHECK(s.action == FLOW_DO_NONE);
    first(&f, x);
    flow_on_play(&f);
    flow_on_status(&f, &x, 0); /* installing */
    s = flow_on_repair(&f);
    CHECK(s.action == FLOW_DO_NONE);
    s = flow_on_install(&f, FLOW_INSTALL_OK);
    CHECK(s.action == FLOW_DO_LAUNCH); /* the Play install still launches */
}

TEST(repair_dropped_when_status_cannot_install)
{
    flow_state f;
    flow_facts x = open_facts(1);
    flow_facts locked = locked_facts(3600);
    flow_facts gone = no_server(1, 1);
    flow_step s;
    first(&f, x);
    flow_on_repair(&f);
    s = flow_on_status(&f, &locked, 0);
    CHECK(s.action == FLOW_DO_WAIT && s.view.screen == THEME_LS_LOCKED);
    first(&f, x);
    flow_on_repair(&f);
    s = flow_on_status(&f, &gone, 0);
    CHECK(waits_on(&s, THEME_LS_READY, "status.offline", FLOW_RETRY_MS)); /* not launched either */
    first(&f, x);
    flow_on_repair(&f);
    x.has_release = 0;
    s = flow_on_status(&f, &x, 0);
    CHECK(waits_on(&s, THEME_LS_READY, "status.no_release", FLOW_RECHECK_MS));
    /* dropped for good: the next status is a plain one */
    x.has_release = 1;
    flow_on_timeout(&f);
    s = flow_on_status(&f, &x, 0);
    CHECK(waits_on(&s, THEME_LS_READY, 0, FLOW_RECHECK_MS));
}

TEST(repair_wins_over_a_click_on_the_same_fetch)
{
    flow_state f;
    flow_facts x = open_facts(1);
    flow_step s;
    first(&f, x);
    flow_on_repair(&f);
    s = flow_on_play(&f); /* while fetching: remembered, then dropped for the repair */
    CHECK(s.action == FLOW_DO_NONE);
    s = flow_on_status(&f, &x, 0);
    CHECK(s.action == FLOW_DO_INSTALL && s.repair);
    s = flow_on_install(&f, FLOW_INSTALL_OK);
    CHECK(s.action == FLOW_DO_FETCH);
    s = flow_on_status(&f, &x, 0);
    CHECK(s.action == FLOW_DO_WAIT);
}

TEST(play_install_is_not_a_repair)
{
    flow_state f;
    flow_facts x = open_facts(0);
    flow_step s;
    first(&f, x);
    flow_on_play(&f);
    s = flow_on_status(&f, &x, 0);
    CHECK(s.action == FLOW_DO_INSTALL && !s.repair && key_is(&s, 0));
}

int main(void)
{
    RUN(start_fetches_while_checking);
    RUN(ready_texts);
    RUN(timeout_polls_without_changing_the_screen);
    RUN(locked_waits);
    RUN(offline_or_error);
    RUN(play_installs_then_launches);
    RUN(play_launches_when_current);
    RUN(install_and_launch_failures);
    RUN(play_ignored_when_disabled);
    RUN(play_ignored_while_installing);
    RUN(h2_click_during_poll_is_served);
    RUN(h2_click_during_first_fetch);
    RUN(h2_pending_click_dropped_when_not_playable);
    RUN(h3_play_fetches_first);
    RUN(h3_fresh_status_decides);
    RUN(h3_retry_after_install_failure_fetches_again);
    RUN(h3_no_server_on_play);
    RUN(h8_no_release);
    RUN(h8_no_release_offline_is_an_error);
    RUN(h11_due_backoff);
    RUN(h11_jitter_bounds);
    RUN(h11_series_restarts_when_state_changes);
    RUN(repair_from_ready_verifies_and_does_not_launch);
    RUN(repair_failure_shows_error_then_recovers);
    RUN(repair_ignored_outside_ready_and_error);
    RUN(repair_dropped_when_status_cannot_install);
    RUN(repair_wins_over_a_click_on_the_same_fetch);
    RUN(play_install_is_not_a_repair);
    return test_report();
}
