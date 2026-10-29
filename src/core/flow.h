/*
 * flow.h - The launcher window's decisions (docs/spec.md, "Launcher flow"), free of threads,
 * clocks and the network so they can be tested on any host.
 *
 * The caller owns a flow_state and drives it with events; every event returns a flow_step:
 * what to do next (fetch the status, wait, install, launch, close) and, when `show` is set,
 * what to put on screen. The window code is only an adapter:
 *
 *     step = flow_start(&f);
 *     loop: apply step.view if step.show, then
 *       FLOW_DO_FETCH:   fetch; if a click arrived meanwhile flow_on_play(&f);
 *                        step = flow_on_status(&f, &facts, random)
 *       FLOW_DO_WAIT:    wait step.wait_ms; on a click step = flow_on_play(&f)
 *                        (FLOW_DO_NONE: keep waiting), on timeout step = flow_on_timeout(&f)
 *       FLOW_DO_INSTALL: step = flow_on_install(&f, result)
 *       FLOW_DO_LAUNCH:  step = flow_on_launch(&f, started)
 *       FLOW_DO_CLOSE:   the game runs: close the launcher
 *
 * Rules: Play always fetches a fresh status first and decides with it; a click that arrives
 * while a status is being fetched is kept and served by that fetch; open without a published
 * release shows status.no_release with Play disabled; a locked server whose countdown is due
 * is polled with growing, jittered delays (5, 10, 20, 40, 60 s + 0-25 %).
 */
#ifndef LAUNCHER_CORE_FLOW_H
#define LAUNCHER_CORE_FLOW_H

#include <stdint.h>

#include "theme.h"

#define FLOW_RECHECK_MS (5u * 60u * 1000u) /* LOCAL: status poll while locked or ready */
#define FLOW_RETRY_MS (30u * 1000u)        /* LOCAL: retry while the server cannot be reached */
#define FLOW_DUE_FIRST_MS 5000u            /* LOCAL: first poll once the countdown is due */
#define FLOW_DUE_MAX_MS 60000u             /* LOCAL: ceiling of the due polls (before jitter) */
#define FLOW_JITTER_PCT 25u                /* LOCAL: due polls get up to this much extra */

/* What the status fetch found. When unreachable, the fields come from the cached status
 * (known = 0 when there is none or it does not parse). */
typedef struct {
    int reachable;        /* the server answered with a valid status just now */
    int known;            /* the fields below hold a status (fresh, or cached when unreachable) */
    int open;
    int has_release;      /* the status names a release to install */
    int64_t secs_to_open; /* status_seconds_to_open: 0 = due, -1 = no date */
    int current;          /* the installed release is the one the status names */
    int interrupted;      /* an install was interrupted (Play resumes it) */
} flow_facts;

typedef enum {
    FLOW_INSTALL_OK = 0,
    FLOW_INSTALL_FAILED,
    FLOW_INSTALL_NO_SPACE
} flow_install_result;

typedef enum {
    FLOW_DO_NONE = 0, /* ignore the event: carry on with what was being done */
    FLOW_DO_FETCH,
    FLOW_DO_WAIT,
    FLOW_DO_INSTALL,
    FLOW_DO_LAUNCH,
    FLOW_DO_CLOSE
} flow_action;

typedef struct {
    theme_launcher_state screen;
    const char *key;      /* status text key, 0 = the theme's status.<screen> */
    const char *fallback; /* built-in English text for key */
    int play_enabled;     /* 0 also in ready when there is nothing to play */
    int64_t countdown;    /* locked: seconds to opening (-1 = no date); else -1 */
} flow_view;

typedef struct {
    flow_action action;
    uint32_t wait_ms; /* FLOW_DO_WAIT */
    int show;         /* view must be applied before doing action */
    flow_view view;
} flow_step;

typedef enum {
    FLOW_PH_IDLE = 0,
    FLOW_PH_FETCHING,
    FLOW_PH_WAITING,
    FLOW_PH_INSTALLING,
    FLOW_PH_LAUNCHING,
    FLOW_PH_DONE
} flow_phase;

/* Owned by the caller; set it up with flow_start. Fields are private to flow.c. */
typedef struct {
    flow_phase phase;
    int play_pending;  /* Play was pressed: the next status decides install/launch */
    uint32_t due_tries; /* consecutive due polls while locked */
    uint32_t wait_ms;  /* poll delay of the last status */
    flow_view view;    /* on screen */
} flow_state;

/* Resets f and asks for the first status (screen checking). */
flow_step flow_start(flow_state *f);

/* A status fetch ended with facts. random: any value from the caller (jitter of the due polls). */
flow_step flow_on_status(flow_state *f, const flow_facts *facts, uint32_t random);

/* Play was pressed. While fetching it is remembered for that fetch (FLOW_DO_NONE); while
 * waiting with Play enabled it asks for a fresh status; otherwise it is ignored. */
flow_step flow_on_play(flow_state *f);

/* The wait ended without a click: poll again (the screen stays). */
flow_step flow_on_timeout(flow_state *f);

flow_step flow_on_install(flow_state *f, flow_install_result result);

/* started: the game process was created. */
flow_step flow_on_launch(flow_state *f, int started);

#endif
