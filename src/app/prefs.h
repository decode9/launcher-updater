/*
 * prefs.h - The user's preferences, .launcher/prefs.json next to the executable (format and
 * validation: core/prefs.h). Today only the language, the single source of truth for every text
 * the launcher shows: the saved choice if there is one, else the Windows UI language.
 *
 * Threads. prefs_load runs once while the process is still single-threaded (app_begin).
 * prefs_language and prefs_set_language may be called from any thread: they only touch the value
 * in memory, behind a spin lock. prefs_save writes the file, so it belongs to whichever thread
 * owns file writes and the log at the time (the window's worker; the UI thread only once the
 * worker is gone): fs.c and log.c are not reentrant.
 */
#ifndef LAUNCHER_APP_PREFS_H
#define LAUNCHER_APP_PREFS_H

#include <stddef.h>

#include "core/prefs.h"

#define PREFS_FILE ".launcher/prefs.json"

/* Reads PREFS_FILE (needs app_dir). Missing or invalid = no preference; never fails. */
void prefs_load(void);

/* The language to use into out (PREFS_LANG_BYTES is enough): the saved one if any, else
 * sys_language. */
void prefs_language(char *out, size_t cap);

/* Chooses lang (prefs_lang_valid, or "" to forget the choice) in memory and marks it unsaved.
 * Returns 0, changing nothing, if lang is not valid. */
int prefs_set_language(const char *lang);

/* Writes PREFS_FILE atomically if the choice changed since the last save (logged). Returns 1 if
 * there was nothing to write or it was written. */
int prefs_save(void);

#endif
