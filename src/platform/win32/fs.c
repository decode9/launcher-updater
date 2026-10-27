/*
 * fs.c - See fs.h.
 */
#include "fs.h"
#include "core/str.h"
#include "win32.h"

#define WIDE_CHARS FS_PATH_BYTES

/* UTF-8 -> UTF-16 into a caller buffer (the callers keep it under 4 KiB of stack). */
static int widen(const char *utf8, wchar_t *out, int cap)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, out, cap);
    return n > 0;
}

int fs_exe_dir(char *out, size_t cap)
{
    wchar_t wide[WIDE_CHARS];
    DWORD n = GetModuleFileNameW(0, wide, WIDE_CHARS);
    int bytes;
    if (n == 0 || n >= WIDE_CHARS) return 0;
    while (n > 0 && wide[n - 1] != '\\' && wide[n - 1] != '/') n--;
    if (n == 0) return 0;
    wide[n - 1] = 0;
    bytes = WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, (int)cap, 0, 0);
    return bytes > 0;
}

int fs_join(char *out, size_t cap, const char *dir, const char *rel)
{
    strbuf sb;
    size_t i;
    sb_init(&sb, out, cap);
    sb_add(&sb, dir);
    sb_add_char(&sb, '\\');
    for (i = 0; rel[i]; i++) sb_add_char(&sb, rel[i] == '/' ? '\\' : rel[i]);
    return sb.ok;
}

static int open_file(fs_file *f, const char *path, DWORD access, DWORD share, DWORD disposition)
{
    wchar_t wide[WIDE_CHARS];
    f->handle = INVALID_HANDLE;
    if (!widen(path, wide, WIDE_CHARS)) return 0;
    f->handle = CreateFileW(wide, access, share, 0, disposition, FILE_ATTRIBUTE_NORMAL, 0);
    return f->handle != INVALID_HANDLE;
}

int fs_open_read(fs_file *f, const char *path)
{
    return open_file(f, path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING);
}

int fs_open_append(fs_file *f, const char *path)
{
    if (!open_file(f, path, GENERIC_WRITE, FILE_SHARE_READ, OPEN_ALWAYS)) return 0;
    if (!SetFilePointerEx(f->handle, 0, 0, FILE_END)) {
        fs_close(f);
        return 0;
    }
    return 1;
}

int fs_create(fs_file *f, const char *path)
{
    return open_file(f, path, GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS);
}

int fs_read(fs_file *f, void *buf, size_t cap, size_t *got)
{
    DWORD n = 0;
    *got = 0;
    if (cap > 0x40000000u) cap = 0x40000000u;
    if (!ReadFile(f->handle, buf, (DWORD)cap, &n, 0)) return 0;
    *got = n;
    return 1;
}

int fs_write(fs_file *f, const void *data, size_t size)
{
    const u8 *p = data;
    while (size) {
        DWORD chunk = size > 0x100000u ? 0x100000u : (DWORD)size, n = 0;
        if (!WriteFile(f->handle, p, chunk, &n, 0) || n == 0) return 0;
        p += n;
        size -= n;
    }
    return 1;
}

int fs_size(fs_file *f, uint64_t *size)
{
    LONGLONG n;
    if (!GetFileSizeEx(f->handle, &n) || n < 0) return 0;
    *size = (uint64_t)n;
    return 1;
}

int fs_flush(fs_file *f)
{
    return FlushFileBuffers(f->handle) != 0;
}

void fs_close(fs_file *f)
{
    if (f->handle != INVALID_HANDLE && f->handle) CloseHandle(f->handle);
    f->handle = INVALID_HANDLE;
}

int fs_read_all(const char *path, void *buf, size_t cap, size_t *len)
{
    fs_file f;
    uint64_t size;
    size_t total = 0, got;
    int ok;
    *len = 0;
    if (!fs_open_read(&f, path)) return 0;
    ok = fs_size(&f, &size) && size <= cap;
    while (ok && total < (size_t)size) {
        ok = fs_read(&f, (u8 *)buf + total, (size_t)size - total, &got) && got > 0;
        total += got;
    }
    fs_close(&f);
    if (ok) *len = total;
    return ok;
}

int fs_open_rw(fs_file *f, const char *path)
{
    return open_file(f, path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, OPEN_ALWAYS);
}

int fs_seek(fs_file *f, uint64_t offset)
{
    return SetFilePointerEx(f->handle, (LONGLONG)offset, 0, FILE_BEGIN) != 0;
}

int fs_truncate(fs_file *f, uint64_t size)
{
    return fs_seek(f, size) && SetEndOfFile(f->handle);
}

int fs_exe_name(char *out, size_t cap)
{
    wchar_t wide[WIDE_CHARS];
    DWORD n = GetModuleFileNameW(0, wide, WIDE_CHARS), start = n;
    if (n == 0 || n >= WIDE_CHARS) return 0;
    while (start > 0 && wide[start - 1] != '\\' && wide[start - 1] != '/') start--;
    return WideCharToMultiByte(CP_UTF8, 0, wide + start, -1, out, (int)cap, 0, 0) > 0;
}

static wchar_t wide_a[WIDE_CHARS], wide_b[WIDE_CHARS];

int fs_rename(const char *from, const char *to)
{
    if (!widen(from, wide_a, WIDE_CHARS) || !widen(to, wide_b, WIDE_CHARS)) return 0;
    return MoveFileExW(wide_a, wide_b, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

int fs_make_parents(const char *path)
{
    int i, last = -1;
    if (!widen(path, wide_a, WIDE_CHARS)) return 0;
    for (i = 0; wide_a[i]; i++)
        if (wide_a[i] == '\\' || wide_a[i] == '/') last = i;
    /* Creates each prefix in turn; "already exists" (or a drive root) is not an error: a missing
     * folder shows up when the file itself is opened. */
    for (i = 1; i <= last; i++) {
        wchar_t c = wide_a[i];
        if (c != '\\' && c != '/') continue;
        if (i > 0 && wide_a[i - 1] == ':') continue;
        wide_a[i] = 0;
        CreateDirectoryW(wide_a, 0);
        wide_a[i] = c;
    }
    return 1;
}

int fs_remove(const char *path)
{
    DWORD e;
    if (!widen(path, wide_a, WIDE_CHARS)) return 0;
    if (DeleteFileW(wide_a)) return 1;
    e = GetLastError();
    return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND;
}

int fs_free_space(const char *dir, uint64_t *bytes)
{
    LONGLONG available;
    if (!widen(dir, wide_a, WIDE_CHARS) || !GetDiskFreeSpaceExW(wide_a, &available, 0, 0) || available < 0) return 0;
    *bytes = (uint64_t)available;
    return 1;
}
