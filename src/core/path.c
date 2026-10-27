/*
 * path.c - See path.h.
 */
#include "path.h"
#include "str.h"

static char upper(char c)
{
    return c >= 'a' && c <= 'z' ? (char)(c - 32) : c;
}

/* CON, PRN, AUX, NUL, COM1-9, LPT1-9, with or without an extension. */
static int is_device_name(const char *s, size_t n)
{
    static const char *const names[] = {"CON", "PRN", "AUX", "NUL"};
    size_t stem = 0, i;
    while (stem < n && s[stem] != '.') stem++;
    if (stem == 3) {
        for (i = 0; i < 4; i++)
            if (upper(s[0]) == names[i][0] && upper(s[1]) == names[i][1] && upper(s[2]) == names[i][2])
                return 1;
    }
    if (stem == 4 && s[3] >= '1' && s[3] <= '9') {
        char a = upper(s[0]), b = upper(s[1]), c = upper(s[2]);
        if ((a == 'C' && b == 'O' && c == 'M') || (a == 'L' && b == 'P' && c == 'T')) return 1;
    }
    return 0;
}

static int component_ok(const char *s, size_t n)
{
    if (n == 0) return 0;                               /* "a//b", leading or trailing '/' */
    if (n == 1 && s[0] == '.') return 0;
    if (n == 2 && s[0] == '.' && s[1] == '.') return 0;
    if (s[n - 1] == '.' || s[n - 1] == ' ') return 0;
    return !is_device_name(s, n);
}

int path_is_safe_relative(const char *path)
{
    size_t i, start = 0, len = str_len(path);
    if (len == 0 || len >= PATH_MAX_BYTES) return 0;
    for (i = 0; i <= len; i++) {
        unsigned char c = (unsigned char)path[i];
        if (c == '/' || c == 0) {
            if (!component_ok(path + start, i - start)) return 0;
            start = i + 1;
            continue;
        }
        if (c < 0x20 || c == 0x7F || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|')
            return 0;
    }
    return 1;
}
