/*
 * sys.h - Process-level services: DLL search hardening, command line, user interface language, UTC clock, exit.
 */
#ifndef LAUNCHER_SYS_H
#define LAUNCHER_SYS_H

#include <stddef.h>

/* Restricts the DLLs Windows loads later at run time (LoadLibrary calls, and the providers that
 * system DLLs such as Schannel, wintrust and GDI+ load on demand) to System32, so a DLL planted
 * next to the exe is never picked up. The static imports are covered by the PE load config
 * (loadcfg.c, /dependentloadflag). Best effort: never fails, call it first in entry(). */
void sys_harden_dll_search(void);

/* 1 if one of the command-line arguments (after the program name) equals flag (ASCII). */
int sys_has_flag(const char *flag);

/* 1 if flag is one of the arguments; then out receives the argument after it as UTF-8
 * (without surrounding quotes), or "" if flag is the last one or is followed by another "--"
 * flag. 0 if flag is absent. */
int sys_flag_value(const char *flag, char *out, size_t cap);

/* ISO 639-1 code of the user's UI language ("en", "es"...), "en" if unknown. */
void sys_language(char *out, size_t cap);

/* Current UTC time as "YYYY-MM-DDTHH:MM:SSZ" (only for log lines, never for game logic). */
void sys_utc_now(char out[21]);

void sys_exit(int code);

#endif
