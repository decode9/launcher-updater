/*
 * sys.c - See sys.h.
 */
#include "sys.h"
#include "core/str.h"
#include "win32.h"

/* Skips one command-line argument following the CommandLineToArgvW quoting rules closely
 * enough for flags: quotes toggle, whitespace ends an unquoted argument. */
static const wchar_t *next_arg(const wchar_t *p, const wchar_t **start, const wchar_t **end)
{
    int quoted = 0;
    while (*p == ' ' || *p == '\t') p++;
    *start = p;
    while (*p && (quoted || (*p != ' ' && *p != '\t'))) {
        if (*p == '"') quoted = !quoted;
        p++;
    }
    *end = p;
    return p;
}

int sys_has_flag(const char *flag)
{
    const wchar_t *p = GetCommandLineW(), *start, *end;
    size_t n = str_len(flag), i;
    p = next_arg(p, &start, &end); /* program name */
    while (*p) {
        p = next_arg(p, &start, &end);
        if ((size_t)(end - start) != n) continue;
        for (i = 0; i < n && start[i] == (wchar_t)(unsigned char)flag[i]; i++) {}
        if (i == n) return 1;
    }
    return 0;
}

int sys_flag_value(const char *flag, char *out, size_t cap)
{
    const wchar_t *p = GetCommandLineW(), *start, *end;
    size_t n = str_len(flag), i;
    int len;
    if (cap) out[0] = 0;
    p = next_arg(p, &start, &end); /* program name */
    while (*p) {
        p = next_arg(p, &start, &end);
        if ((size_t)(end - start) != n) continue;
        for (i = 0; i < n && start[i] == (wchar_t)(unsigned char)flag[i]; i++) {}
        if (i != n) continue;
        next_arg(p, &start, &end);
        if (end > start && *start == '"') start++;
        if (end > start && end[-1] == '"') end--;
        if (end == start || !cap || (end - start >= 2 && start[0] == '-' && start[1] == '-')) return 1;
        len = WideCharToMultiByte(CP_UTF8, 0, start, (int)(end - start), out, (int)cap - 1, 0, 0);
        out[len > 0 ? len : 0] = 0;
        return 1;
    }
    return 0;
}

void sys_language(char *out, size_t cap)
{
    wchar_t name[16];
    int n = GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SISO639LANGNAME, name, 16), i;
    if (n <= 1 || (size_t)n > cap) {
        str_copy(out, cap, "en");
        return;
    }
    for (i = 0; i < n; i++) {
        wchar_t c = name[i];
        if (c && !(c >= 'a' && c <= 'z')) {
            str_copy(out, cap, "en");
            return;
        }
        out[i] = (char)c;
    }
}

static void two(char *out, unsigned v)
{
    out[0] = (char)('0' + v / 10 % 10);
    out[1] = (char)('0' + v % 10);
}

void sys_utc_now(char out[21])
{
    SYSTEMTIME t;
    GetSystemTime(&t);
    two(out, t.year / 100);
    two(out + 2, t.year % 100);
    out[4] = '-';
    two(out + 5, t.month);
    out[7] = '-';
    two(out + 8, t.day);
    out[10] = 'T';
    two(out + 11, t.hour);
    out[13] = ':';
    two(out + 14, t.minute);
    out[16] = ':';
    two(out + 17, t.second);
    out[19] = 'Z';
    out[20] = 0;
}

void sys_exit(int code)
{
    ExitProcess((unsigned)code);
}
