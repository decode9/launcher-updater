/*
 * app.h - Orchestration: the launcher's modes (main.c picks one from the command line).
 */
#ifndef LAUNCHER_APP_H
#define LAUNCHER_APP_H

/* launcher.exe --status: read launcher.json, GET /v1/status, log the result to launcher.log.
 * Returns 0 status read, 1 configuration error, 2 network/server error. */
int app_run(int status_only);

/* launcher.exe --install: status, then the release manifest, then the install engine
 * (core/install.h), logging progress to launcher.log. Returns 0 installed (or already current),
 * 1 configuration error, 2 network/server error, 3 install error. */
int app_install(void);

/* launcher.exe (no arguments): the themed window (window.c). */
int app_window(void);

/* launcher.exe --swap <pid>: second half of self-update (selfupdate.c). */
int app_swap(void);

#endif
