/*
 * preview.h - The theme preview mode (launcher.exe --theme-preview [folder]), see preview.c.
 */
#ifndef LAUNCHER_APP_PREVIEW_H
#define LAUNCHER_APP_PREVIEW_H

/* Shows the theme of the --theme-preview folder (or the built-in one) cycling through the
 * launcher states until the window is closed. Returns the process exit code. */
int app_preview(void);

#endif
