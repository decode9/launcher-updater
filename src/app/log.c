/*
 * log.c - See log.h.
 */
#include "log.h"
#include "platform/win32/fs.h"
#include "platform/win32/sys.h"

static fs_file file;
static int is_open;
static char line[2048];
static strbuf builder;

void log_open(const char *path)
{
    is_open = fs_open_append(&file, path);
}

strbuf *log_begin(void)
{
    char now[21];
    sys_utc_now(now);
    sb_init(&builder, line, sizeof line - 2);
    sb_add(&builder, now);
    sb_add_char(&builder, ' ');
    return &builder;
}

void log_end(void)
{
    size_t n = builder.len;
    if (!is_open) return;
    line[n++] = '\r'; /* the builder kept two spare bytes, so a truncated line still ends */
    line[n++] = '\n';
    fs_write(&file, line, n);
}

void log_line(const char *text)
{
    sb_add(log_begin(), text);
    log_end();
}

void log_close(void)
{
    if (is_open) fs_close(&file);
    is_open = 0;
}
