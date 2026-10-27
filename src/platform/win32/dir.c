/*
 * dir.c - See dir.h. Its kernel32 imports are listed in kernel32_dir.def.
 */
#include "dir.h"
#include "core/str.h"
#include "fs.h"
#include "win32.h"

#define FILE_ATTRIBUTE_DIRECTORY 0x10

typedef struct {
    DWORD low, high;
} FILETIME;

typedef struct {
    DWORD attributes;
    FILETIME created, accessed, written;
    DWORD size_high, size_low, reserved0, reserved1;
    wchar_t name[260];
    wchar_t short_name[14];
} WIN32_FIND_DATAW;

IMPORT HANDLE WINAPI FindFirstFileW(const wchar_t *, WIN32_FIND_DATAW *);
IMPORT BOOL WINAPI FindNextFileW(HANDLE, WIN32_FIND_DATAW *);
IMPORT BOOL WINAPI FindClose(HANDLE);

/* Static (592 bytes); FindNextFileW only writes it, so a nested listing from the callback is
 * harmless. */
static WIN32_FIND_DATAW g_find;

int dir_list(const char *dir, dir_entry_fn fn, void *ctx)
{
    static wchar_t pattern[FS_PATH_BYTES];
    char path[FS_PATH_BYTES], name[FS_PATH_BYTES], spec[FS_PATH_BYTES];
    HANDLE h;
    strbuf sb;
    sb_init(&sb, spec, sizeof spec);
    sb_add(&sb, dir);
    sb_add(&sb, "\\*");
    if (!sb.ok) return 0;
    if (MultiByteToWideChar(CP_UTF8, 0, spec, -1, pattern, FS_PATH_BYTES) <= 0) return 0;
    h = FindFirstFileW(pattern, &g_find);
    if (h == INVALID_HANDLE) return 0;
    do {
        const wchar_t *n = g_find.name;
        int is_dir = (g_find.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (n[0] == '.' && (n[1] == 0 || (n[1] == '.' && n[2] == 0))) continue;
        if (WideCharToMultiByte(CP_UTF8, 0, n, -1, name, sizeof name, 0, 0) <= 0) continue;
        if (!fs_join(path, sizeof path, dir, name)) continue;
        if (!fn(ctx, path, is_dir)) break;
    } while (FindNextFileW(h, &g_find));
    FindClose(h);
    return 1;
}
