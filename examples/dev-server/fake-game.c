/*
 * fake-game.c - A stand-in "game" for trying the launcher: writes its command line to
 * fake-game.txt in the current folder and exits. Build (same toolchain as build.sh):
 *   clang --target=i686-pc-windows-msvc -O2 -ffreestanding -fno-builtin -c fake-game.c -o fake-game.obj
 *   llvm-dlltool -m i386 -k -d ../../src/platform/win32/kernel32.def -l kernel32.lib
 *   lld-link /nologo /machine:x86 /subsystem:console /nodefaultlib /entry:entry fake-game.obj kernel32.lib /out:Game.exe
 */
#include <stddef.h>

typedef void *HANDLE;
typedef unsigned long DWORD;
__declspec(dllimport) wchar_t *__stdcall GetCommandLineW(void);
__declspec(dllimport) HANDLE __stdcall CreateFileW(const wchar_t *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
__declspec(dllimport) int __stdcall WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
__declspec(dllimport) int __stdcall CloseHandle(HANDLE);
__declspec(dllimport) void __stdcall ExitProcess(unsigned);

void entry(void)
{
    const wchar_t *cmd = GetCommandLineW();
    DWORD n = 0, written;
    HANDLE f = CreateFileW(L"fake-game.txt", 0x40000000UL, 0, 0, 2 /* CREATE_ALWAYS */, 0x80, 0);
    while (cmd[n]) n++;
    if (f != (HANDLE)-1) {
        WriteFile(f, "\xFF\xFE", 2, &written, 0); /* UTF-16LE text */
        WriteFile(f, cmd, n * 2, &written, 0);
        CloseHandle(f);
    }
    ExitProcess(0);
}
