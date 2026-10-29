/*
 * instance.h - One launcher per folder: a named mutex keyed on the launcher folder, so two
 * launchers (a double click, a relaunch during self-update) never install over the same
 * .launcher/state.json, journals, .part files or launcher.exe.new at once.
 *
 * The mutex is Local\decode9-launcher-<key>, where <key> is the first 16 hex digits of the
 * SHA-256 of the folder path as UTF-8 with ASCII letters lowered. Lowering ASCII only is enough:
 * the path comes from GetModuleFileNameW, which spells a folder the same way every time it is
 * started from it, and the exceptions (another letter case typed in a shortcut) are ASCII in
 * practice. "Local\" scopes it to the user's session.
 *
 * Ownership belongs to the thread that took it and is dropped when the process exits (even if
 * it crashes), so callers only release it early when another process must take it next.
 */
#ifndef LAUNCHER_INSTANCE_H
#define LAUNCHER_INSTANCE_H

#include <stdint.h>

#define INSTANCE_KEY_CHARS 16

typedef enum {
    INSTANCE_OK,   /* this process holds the folder */
    INSTANCE_BUSY, /* another process held it for all of wait_ms */
    INSTANCE_ERROR /* the mutex could not be created (the caller decides; nothing is held) */
} instance_result;

/* Takes the folder's mutex, waiting up to wait_ms for its holder to finish (0: do not wait).
 * dir is the launcher folder (UTF-8). Call once per process. */
instance_result instance_acquire(const char *dir, uint32_t wait_ms);

/* The folder key ("" until instance_acquire ran): the UI names its window class after it so
 * the launcher of this folder can be found (ui_bring_to_front). */
const char *instance_key(void);

/* Releases the mutex if this thread holds it, and closes it. Safe to call when not held. */
void instance_release(void);

#endif
