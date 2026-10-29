/*
 * preview.h - The theme preview mode (launcher.exe --theme-preview [folder]), see preview.c.
 */
#ifndef LAUNCHER_APP_PREVIEW_H
#define LAUNCHER_APP_PREVIEW_H

/* Shows the theme of the --theme-preview folder (or the built-in one) cycling through the
 * launcher states until the window is closed. Returns the process exit code. */
int app_preview(void);

/* The preview's language menu, UI thread only: shows the theme texts and title in lang
 * (core/prefs.h prefs_lang_valid) from now on. Nothing is saved: the preview has no launcher
 * folder. Returns 0, changing nothing, if lang is not valid. */
int preview_set_language(const char *lang);

#endif
