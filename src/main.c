/*
 * main.c - Entry point of the GUI executable (no C runtime: lld-link /entry:entry).
 *
 *   launcher.exe            the themed window: status, theme, news, install, play (app/window.c)
 *   launcher.exe --status   check the server, write launcher.log and exit
 *   launcher.exe --install  check the server, install or update the release, log and exit
 *   launcher.exe --swap <pid>  second half of self-update (app/selfupdate.c)
 *   launcher.exe --theme-preview [folder]  show a theme without a server (app/preview.c)
 */
#include "app/app.h"
#include "app/preview.h"
#include "platform/win32/sys.h"

void entry(void)
{
    if (sys_has_flag("--theme-preview")) sys_exit(app_preview());
    if (sys_has_flag("--swap")) sys_exit(app_swap());
    if (sys_has_flag("--install")) sys_exit(app_install());
    if (sys_has_flag("--status")) sys_exit(app_run(1));
    sys_exit(app_window());
}
