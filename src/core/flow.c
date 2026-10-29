/*
 * flow.c - See flow.h.
 */
#include "flow.h"

static flow_view view_of(theme_launcher_state screen, const char *key, const char *fallback, int play)
{
    flow_view v;
    v.screen = screen;
    v.key = key;
    v.fallback = fallback;
    v.play_enabled = play;
    v.countdown = -1;
    return v;
}

static flow_step step_of(flow_action action, uint32_t wait_ms)
{
    flow_step s;
    s.action = action;
    s.wait_ms = wait_ms;
    s.repair = 0;
    s.show = 0;
    s.view = view_of(THEME_LS_CHECKING, 0, 0, 0);
    return s;
}

/* Puts view on screen (remembered in f) and waits ms. */
static flow_step show_and_wait(flow_state *f, flow_view view, uint32_t ms)
{
    flow_step s = step_of(FLOW_DO_WAIT, ms);
    f->phase = FLOW_PH_WAITING;
    f->view = view;
    s.show = 1;
    s.view = view;
    return s;
}

static flow_step fetch(flow_state *f, int show_checking)
{
    flow_step s = step_of(FLOW_DO_FETCH, 0);
    f->phase = FLOW_PH_FETCHING;
    if (show_checking) {
        f->view = s.view;
        s.show = 1;
    }
    return s;
}

static flow_step install(flow_state *f, int repair)
{
    flow_step s = step_of(FLOW_DO_INSTALL, 0);
    f->phase = FLOW_PH_INSTALLING;
    f->repairing = repair;
    f->view = repair ? view_of(THEME_LS_INSTALLING, "status.repairing", "Checking and repairing the game files...", 0)
                     : view_of(THEME_LS_INSTALLING, 0, 0, 0);
    s.repair = repair;
    s.show = 1;
    s.view = f->view;
    return s;
}

static flow_step launch(flow_state *f)
{
    f->phase = FLOW_PH_LAUNCHING;
    return step_of(FLOW_DO_LAUNCH, 0); /* the screen stays as it is */
}

/* ---- what to show ------------------------------------------------------------------- */

static flow_view ready_view(const flow_facts *facts)
{
    if (!facts->has_release)
        return view_of(THEME_LS_READY, "status.no_release", "No version has been published yet.", 0);
    if (facts->current) return view_of(THEME_LS_READY, 0, 0, 1);
    if (facts->interrupted)
        return view_of(THEME_LS_READY, "status.resume", "The install was interrupted: Play resumes it.", 1);
    return view_of(THEME_LS_READY, "status.update", "An update is available: Play installs it first.", 1);
}

static flow_view offline_view(void)
{
    return view_of(THEME_LS_READY, "status.offline", "Offline: you can play the installed version.", 1);
}

static flow_view error_view(void)
{
    return view_of(THEME_LS_ERROR, "status.connection", "Cannot reach the server. Retrying in 30 seconds.", 0);
}

static flow_view locked_view(int64_t secs)
{
    flow_view v = view_of(THEME_LS_LOCKED, 0, 0, 0);
    v.countdown = secs;
    return v;
}

/* ---- how long to wait --------------------------------------------------------------- */

/* 5, 10, 20, 40, 60, 60... seconds for try 0, 1, 2... plus 0..FLOW_JITTER_PCT % from random. */
static uint32_t due_delay_ms(uint32_t tries, uint32_t random)
{
    uint32_t ms = FLOW_DUE_FIRST_MS;
    uint32_t quarter;
    while (tries-- && ms < FLOW_DUE_MAX_MS) ms <<= 1;
    if (ms > FLOW_DUE_MAX_MS) ms = FLOW_DUE_MAX_MS;
    quarter = ms / 100u * FLOW_JITTER_PCT;       /* at most 15000 */
    return ms + quarter * (random & 0xFFFFu) / 0x10000u; /* 15000 * 65535 fits in 32 bits */
}

/* Locked: poll when the countdown ends (secs + 1 s, at least the first due delay) if that is
 * within the recheck period, the growing due delay once it is due, else every recheck period
 * (also with no date). */
static uint32_t locked_delay_ms(flow_state *f, int64_t secs, uint32_t random)
{
    if (secs == 0) return due_delay_ms(f->due_tries++, random);
    f->due_tries = 0;
    if (secs > 0 && secs < (int64_t)(FLOW_RECHECK_MS / 1000u)) {
        uint32_t ms = (uint32_t)secs * 1000u + 1000u;
        return ms < FLOW_DUE_FIRST_MS ? FLOW_DUE_FIRST_MS : ms;
    }
    return FLOW_RECHECK_MS;
}

/* ---- what to do --------------------------------------------------------------------- */

static int can_play(const flow_facts *facts)
{
    return facts->known && facts->open && facts->has_release;
}

static flow_step on_unreachable(flow_state *f, const flow_facts *facts, int play)
{
    int offline = can_play(facts) && facts->current;
    f->due_tries = 0;
    f->wait_ms = FLOW_RETRY_MS;
    if (!offline) return show_and_wait(f, error_view(), FLOW_RETRY_MS);
    if (play) return launch(f); /* the installed release, without installing */
    return show_and_wait(f, offline_view(), FLOW_RETRY_MS);
}

static flow_step on_fresh(flow_state *f, const flow_facts *facts, int play, int repair, uint32_t random)
{
    if (!facts->open) {
        uint32_t ms = locked_delay_ms(f, facts->secs_to_open, random);
        f->wait_ms = ms;
        return show_and_wait(f, locked_view(facts->secs_to_open), ms);
    }
    f->due_tries = 0;
    f->wait_ms = FLOW_RECHECK_MS;
    if (repair && facts->has_release) return install(f, 1);
    if (play && facts->has_release) return facts->current ? launch(f) : install(f, 0);
    return show_and_wait(f, ready_view(facts), FLOW_RECHECK_MS);
}

flow_step flow_start(flow_state *f)
{
    f->play_pending = 0;
    f->repair_pending = 0;
    f->repairing = 0;
    f->due_tries = 0;
    f->wait_ms = FLOW_RECHECK_MS;
    return fetch(f, 1);
}

flow_step flow_on_status(flow_state *f, const flow_facts *facts, uint32_t random)
{
    int repair = f->repair_pending;
    int play = f->play_pending && !repair; /* a repair wins over a click on the same fetch */
    f->play_pending = 0; /* served by this status, or dropped when it cannot be played */
    f->repair_pending = 0;
    if (!facts->reachable) return on_unreachable(f, facts, play);
    return on_fresh(f, facts, play, repair, random);
}

flow_step flow_on_play(flow_state *f)
{
    if (f->phase == FLOW_PH_FETCHING) {
        f->play_pending = 1;
        return step_of(FLOW_DO_NONE, 0);
    }
    if (f->phase != FLOW_PH_WAITING || !f->view.play_enabled) return step_of(FLOW_DO_NONE, 0);
    f->play_pending = 1;
    return fetch(f, 1);
}

flow_step flow_on_repair(flow_state *f)
{
    if (f->phase != FLOW_PH_WAITING || (f->view.screen != THEME_LS_READY && f->view.screen != THEME_LS_ERROR))
        return step_of(FLOW_DO_NONE, 0);
    f->repair_pending = 1;
    f->play_pending = 0;
    return fetch(f, 1);
}

flow_step flow_on_timeout(flow_state *f)
{
    return fetch(f, 0);
}

/* A repair never launches: success re-reads the status, which shows the usual screen; failure
 * shows the error screen until the next poll (FLOW_RETRY_MS), where repair can be retried. */
static flow_step repair_done(flow_state *f, flow_install_result result)
{
    f->repairing = 0;
    if (result == FLOW_INSTALL_OK) return fetch(f, 1);
    if (result == FLOW_INSTALL_NO_SPACE)
        return show_and_wait(f, view_of(THEME_LS_ERROR, "status.no_space", "Not enough free disk space for the update.", 0),
                             FLOW_RETRY_MS);
    return show_and_wait(f, view_of(THEME_LS_ERROR, "status.repair_failed", "The repair failed. Try again from Options.", 0),
                         FLOW_RETRY_MS);
}

flow_step flow_on_install(flow_state *f, flow_install_result result)
{
    if (f->repairing) return repair_done(f, result);
    if (result == FLOW_INSTALL_OK) return launch(f);
    if (result == FLOW_INSTALL_NO_SPACE)
        return show_and_wait(f, view_of(THEME_LS_READY, "status.no_space", "Not enough free disk space for the update.", 1),
                             f->wait_ms);
    return show_and_wait(f, view_of(THEME_LS_READY, "status.install_failed", "The update failed. Press Play to try again.", 1),
                         f->wait_ms);
}

flow_step flow_on_launch(flow_state *f, int started)
{
    if (started) {
        f->phase = FLOW_PH_DONE;
        return step_of(FLOW_DO_CLOSE, 0);
    }
    return show_and_wait(f, view_of(THEME_LS_READY, "status.launch_failed", "The game could not be started.", 1),
                         f->wait_ms);
}
