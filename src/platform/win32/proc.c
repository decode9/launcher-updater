/*
 * proc.c - See proc.h.
 */
#include "proc.h"
#include "core/str.h"
#include "fs.h"
#include "rt.h"
#include "win32.h"

#define SYNCHRONIZE_ACCESS 0x00100000UL
#define WAIT_OBJECT_0 0
#define CMDLINE_BYTES 32768

typedef struct {
    DWORD cb;
    wchar_t *reserved, *desktop, *title;
    DWORD x, y, x_size, y_size, x_chars, y_chars, fill, flags;
    WORD show_window, reserved2_size;
    u8 *reserved2;
    HANDLE std_input, std_output, std_error;
} STARTUPINFOW;

typedef struct {
    HANDLE process, thread;
    DWORD process_id, thread_id;
} PROCESS_INFORMATION;

IMPORT BOOL WINAPI CreateProcessW(const wchar_t *, wchar_t *, void *, void *, BOOL, DWORD, void *, const wchar_t *,
                                  STARTUPINFOW *, PROCESS_INFORMATION *);
IMPORT HANDLE WINAPI OpenProcess(DWORD, BOOL, DWORD);
IMPORT DWORD WINAPI GetCurrentProcessId(void);
IMPORT DWORD WINAPI WaitForSingleObject(HANDLE, DWORD);

static char cmd_utf8[CMDLINE_BYTES];
static wchar_t cmd_wide[CMDLINE_BYTES];
static wchar_t exe_wide[FS_PATH_BYTES], dir_wide[FS_PATH_BYTES];

/* Appends one argument with the CommandLineToArgvW quoting rules. Plain arguments go as they
 * are (so simple flag parsers such as sys_has_flag see them unchanged); others are quoted,
 * with backslashes doubled before a quote (and before the closing quote) and " as \". */
static void add_quoted(strbuf *sb, const char *arg)
{
    size_t slashes = 0, i;
    const char *p;
    int plain = *arg != 0;
    for (p = arg; *p; p++)
        if (*p == ' ' || *p == '\t' || *p == '"') plain = 0;
    if (plain) {
        sb_add(sb, arg);
        return;
    }
    sb_add_char(sb, '"');
    for (; *arg; arg++) {
        if (*arg == '\\') {
            slashes++;
            continue;
        }
        if (*arg == '"') {
            for (i = 0; i < slashes * 2 + 1; i++) sb_add_char(sb, '\\');
        } else {
            for (i = 0; i < slashes; i++) sb_add_char(sb, '\\');
        }
        slashes = 0;
        sb_add_char(sb, *arg);
    }
    for (i = 0; i < slashes * 2; i++) sb_add_char(sb, '\\');
    sb_add_char(sb, '"');
}

static int widen(const char *utf8, wchar_t *out, int cap)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, out, cap);
    return n > 0;
}

int proc_launch(const char *exe, const char *const *args, uint32_t argc, const char *workdir)
{
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    strbuf sb;
    uint32_t i;
    sb_init(&sb, cmd_utf8, sizeof cmd_utf8);
    add_quoted(&sb, exe);
    for (i = 0; i < argc; i++) {
        sb_add_char(&sb, ' ');
        add_quoted(&sb, args[i]);
    }
    if (!sb.ok || !widen(cmd_utf8, cmd_wide, CMDLINE_BYTES) || !widen(exe, exe_wide, FS_PATH_BYTES) ||
        !widen(workdir, dir_wide, FS_PATH_BYTES))
        return 0;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    memset(&pi, 0, sizeof pi);
    if (!CreateProcessW(exe_wide, cmd_wide, 0, 0, 0, 0, 0, dir_wide, &si, &pi)) return 0;
    CloseHandle(pi.thread);
    CloseHandle(pi.process);
    return 1;
}

uint32_t proc_current_id(void)
{
    return (uint32_t)GetCurrentProcessId();
}

int proc_wait_exit(uint32_t pid, uint32_t ms)
{
    HANDLE h = OpenProcess(SYNCHRONIZE_ACCESS, 0, (DWORD)pid);
    int done;
    if (!h) return 1;
    done = WaitForSingleObject(h, (DWORD)ms) == WAIT_OBJECT_0;
    CloseHandle(h);
    return done;
}
