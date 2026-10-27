/*
 * win32.h - The Win32 types and kernel32/user32 imports the launcher uses, declared by hand so
 * the build needs no Windows SDK. Every import here must also be listed in its .def file
 * (the .def files in src/platform/win32), which build.sh turns into an import library.
 * Only src/platform/win32 includes this header; the core and the app never see Win32 types.
 */
#ifndef LAUNCHER_WIN32_H
#define LAUNCHER_WIN32_H

#include <stddef.h>
#include <stdint.h>

typedef unsigned long DWORD;
typedef int BOOL;
typedef void *HANDLE;
typedef unsigned short WORD;
typedef unsigned short u16;
typedef unsigned char u8;
typedef long long LONGLONG;

#define WINAPI __stdcall
#define IMPORT __declspec(dllimport)
#define INVALID_HANDLE ((HANDLE)(intptr_t)-1)

#define GENERIC_READ 0x80000000UL
#define GENERIC_WRITE 0x40000000UL
#define FILE_SHARE_READ 1
#define FILE_SHARE_WRITE 2
#define FILE_SHARE_DELETE 4
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define FILE_ATTRIBUTE_NORMAL 0x80
#define FILE_BEGIN 0
#define FILE_END 2
#define MOVEFILE_REPLACE_EXISTING 1
#define MOVEFILE_WRITE_THROUGH 8
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_PATH_NOT_FOUND 3
#define CP_UTF8 65001
#define MB_ERR_INVALID_CHARS 8
#define LOCALE_USER_DEFAULT 0x0400
#define LOCALE_SISO639LANGNAME 0x59

typedef struct {
    WORD year, month, day_of_week, day, hour, minute, second, milliseconds;
} SYSTEMTIME;

IMPORT DWORD WINAPI GetModuleFileNameW(HANDLE, wchar_t *, DWORD);
IMPORT wchar_t *WINAPI GetCommandLineW(void);
IMPORT void WINAPI ExitProcess(unsigned);
IMPORT DWORD WINAPI GetLastError(void);
IMPORT HANDLE WINAPI CreateFileW(const wchar_t *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
IMPORT BOOL WINAPI ReadFile(HANDLE, void *, DWORD, DWORD *, void *);
IMPORT BOOL WINAPI WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
IMPORT BOOL WINAPI GetFileSizeEx(HANDLE, LONGLONG *);
IMPORT BOOL WINAPI SetFilePointerEx(HANDLE, LONGLONG, LONGLONG *, DWORD);
IMPORT BOOL WINAPI FlushFileBuffers(HANDLE);
IMPORT BOOL WINAPI CloseHandle(HANDLE);
IMPORT BOOL WINAPI SetEndOfFile(HANDLE);
IMPORT BOOL WINAPI MoveFileExW(const wchar_t *, const wchar_t *, DWORD);
IMPORT BOOL WINAPI CreateDirectoryW(const wchar_t *, void *);
IMPORT BOOL WINAPI DeleteFileW(const wchar_t *);
IMPORT BOOL WINAPI GetDiskFreeSpaceExW(const wchar_t *, LONGLONG *, LONGLONG *, LONGLONG *);
IMPORT void WINAPI GetSystemTime(SYSTEMTIME *);
IMPORT DWORD WINAPI GetTickCount(void);
IMPORT int WINAPI MultiByteToWideChar(unsigned, DWORD, const char *, int, wchar_t *, int);
IMPORT int WINAPI WideCharToMultiByte(unsigned, DWORD, const wchar_t *, int, char *, int, const char *, BOOL *);
IMPORT int WINAPI GetLocaleInfoW(DWORD, DWORD, wchar_t *, int);

#endif
