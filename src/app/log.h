/*
 * log.h - Append-only text log (launcher.log next to the executable). Every line starts with a
 * UTC timestamp. Logging never fails the caller: if the file cannot be opened, lines are dropped.
 */
#ifndef LAUNCHER_APP_LOG_H
#define LAUNCHER_APP_LOG_H

#include "core/str.h"

void log_open(const char *path);
void log_line(const char *text);
void log_close(void);

/* Builder for one line: log_begin() returns a strbuf; log_end() writes it. */
strbuf *log_begin(void);
void log_end(void);

#endif
