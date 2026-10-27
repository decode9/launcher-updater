/*
 * proc.h - Starting processes (the game, the swap step of self-update) and waiting for one to
 * exit. Paths and arguments are UTF-8.
 */
#ifndef LAUNCHER_PROC_H
#define LAUNCHER_PROC_H

#include <stdint.h>

/* Starts exe (native path) with args, quoted so the child's CommandLineToArgvW / C runtime
 * gets them back exactly, in the working folder workdir. Does not wait. Returns 1 on success. */
int proc_launch(const char *exe, const char *const *args, uint32_t argc, const char *workdir);

uint32_t proc_current_id(void);

/* Waits up to ms milliseconds for process pid to exit. Returns 1 if it has exited (or no
 * longer exists), 0 on timeout. */
int proc_wait_exit(uint32_t pid, uint32_t ms);

#endif
