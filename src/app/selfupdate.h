/*
 * selfupdate.h - The launcher replaces itself when /v1/status announces a newer version
 * (docs/spec.md, "Self-update"):
 *
 *   1. download the `launcher` blob, check its size and SHA-256;
 *   2. check the signer (see selfupdate.c) and write "<exe>.new" next to the executable;
 *   3. start "<exe>.new --swap <pid>" and exit;
 *   4. the new process waits for <pid> to exit, renames itself over <exe> (with retries), starts
 *      "<exe> --skip-update" and exits;
 *   5. the next normal start removes a leftover "<exe>.new".
 * Any failure keeps the running version, which carries on (and logs why).
 */
#ifndef LAUNCHER_APP_SELFUPDATE_H
#define LAUNCHER_APP_SELFUPDATE_H

/* 1 if app_status.launcher is newer than LAUNCHER_VERSION. */
int selfupdate_available(void);

/* Steps 1-3. Returns 1 if the new launcher was started: the caller must then exit promptly
 * (the new one waits for this process). Worker thread. */
int selfupdate_start(void);

/* Step 5: removes "<exe>.new" if it is there. */
void selfupdate_cleanup(void);

#endif
