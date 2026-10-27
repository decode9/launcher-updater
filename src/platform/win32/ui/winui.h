/*
 * winui.h - The user32, gdi32, shell32, shlwapi (and a few kernel32) declarations the window
 * code uses, written by hand like win32.h so the build needs no Windows SDK. Every import must
 * also be listed in its .def file in src/platform/win32 (user32.def, gdi32.def, shell32.def,
 * shlwapi.def).
 */
#ifndef LAUNCHER_WIN32_UI_WINUI_H
#define LAUNCHER_WIN32_UI_WINUI_H

#include "platform/win32/win32.h"

typedef void *HWND;
typedef void *HDC;
typedef void *HGDIOBJ;
typedef void *HBITMAP;
typedef void *HRGN;
typedef void *HCURSOR;
typedef void *HICON;
typedef void *HINSTANCE;
typedef unsigned int UINT;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef intptr_t LRESULT;
typedef long LONG;
typedef LRESULT(WINAPI *WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef void(WINAPI *TIMERPROC)(HWND, UINT, uintptr_t, DWORD);

typedef struct {
    LONG x, y;
} POINT;

typedef struct {
    LONG left, top, right, bottom;
} RECT;

typedef struct {
    HWND hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD time;
    POINT pt;
} MSG;

typedef struct {
    UINT cbSize, style;
    WNDPROC lpfnWndProc;
    int cbClsExtra, cbWndExtra;
    HINSTANCE hInstance;
    HICON hIcon;
    HCURSOR hCursor;
    void *hbrBackground;
    const wchar_t *lpszMenuName, *lpszClassName;
    HICON hIconSm;
} WNDCLASSEXW;

typedef struct {
    HDC hdc;
    BOOL fErase;
    RECT rcPaint;
    BOOL fRestore, fIncUpdate;
    unsigned char rgbReserved[32];
} PAINTSTRUCT;

typedef struct {
    DWORD cbSize, dwFlags;
    HWND hwndTrack;
    DWORD dwHoverTime;
} TRACKMOUSEEVENT;

/* COM IStream: only Release is called directly; GDI+ uses the rest. */
typedef struct IStream IStream;
typedef struct {
    void *QueryInterface;
    unsigned long(WINAPI *AddRef)(IStream *);
    unsigned long(WINAPI *Release)(IStream *);
} IStreamVtbl;
struct IStream {
    const IStreamVtbl *lpVtbl;
};

#define WM_DESTROY 0x0002
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_ERASEBKGND 0x0014
#define WM_SETCURSOR 0x0020
#define WM_SETICON 0x0080
#define WM_NCHITTEST 0x0084
#define WM_NCMOUSEMOVE 0x00A0
#define WM_NCLBUTTONDBLCLK 0x00A3
#define WM_TIMER 0x0113
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_CAPTURECHANGED 0x0215
#define WM_MOUSELEAVE 0x02A3
#define WM_APP 0x8000

#define HTCLIENT 1
#define HTCAPTION 2
#define TME_LEAVE 0x2
#define IDC_ARROW ((const wchar_t *)(uintptr_t)32512)
#define IDC_HAND ((const wchar_t *)(uintptr_t)32649)
#define SW_SHOWNORMAL 1
#define SW_SHOW 5
#define SW_MINIMIZE 6
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define SWP_NOZORDER 0x0004
#define SWP_NOACTIVATE 0x0010
#define WS_POPUP 0x80000000UL
#define WS_SYSMENU 0x00080000UL
#define WS_MINIMIZEBOX 0x00020000UL
#define WS_EX_APPWINDOW 0x00040000UL
#define SRCCOPY 0x00CC0020UL
#define ICON_SMALL 0
#define ICON_BIG 1

IMPORT unsigned short WINAPI RegisterClassExW(const WNDCLASSEXW *);
IMPORT HWND WINAPI CreateWindowExW(DWORD, const wchar_t *, const wchar_t *, DWORD, int, int, int, int, HWND,
                                   void *, HINSTANCE, void *);
IMPORT LRESULT WINAPI DefWindowProcW(HWND, UINT, WPARAM, LPARAM);
IMPORT BOOL WINAPI DestroyWindow(HWND);
IMPORT BOOL WINAPI GetMessageW(MSG *, HWND, UINT, UINT);
IMPORT BOOL WINAPI TranslateMessage(const MSG *);
IMPORT LRESULT WINAPI DispatchMessageW(const MSG *);
IMPORT BOOL WINAPI PostMessageW(HWND, UINT, WPARAM, LPARAM);
IMPORT void WINAPI PostQuitMessage(int);
IMPORT LRESULT WINAPI SendMessageW(HWND, UINT, WPARAM, LPARAM);
IMPORT BOOL WINAPI ShowWindow(HWND, int);
IMPORT BOOL WINAPI UpdateWindow(HWND);
IMPORT BOOL WINAPI InvalidateRect(HWND, const RECT *, BOOL);
IMPORT HDC WINAPI BeginPaint(HWND, PAINTSTRUCT *);
IMPORT BOOL WINAPI EndPaint(HWND, const PAINTSTRUCT *);
IMPORT HDC WINAPI GetDC(HWND);
IMPORT int WINAPI ReleaseDC(HWND, HDC);
IMPORT uintptr_t WINAPI SetTimer(HWND, uintptr_t, UINT, TIMERPROC);
IMPORT BOOL WINAPI KillTimer(HWND, uintptr_t);
IMPORT int WINAPI SetWindowRgn(HWND, HRGN, BOOL);
IMPORT BOOL WINAPI SetWindowPos(HWND, HWND, int, int, int, int, UINT);
IMPORT BOOL WINAPI SetWindowTextW(HWND, const wchar_t *);
IMPORT HCURSOR WINAPI LoadCursorW(HINSTANCE, const wchar_t *);
IMPORT HICON WINAPI LoadIconW(HINSTANCE, const wchar_t *);
IMPORT HCURSOR WINAPI SetCursor(HCURSOR);
IMPORT BOOL WINAPI TrackMouseEvent(TRACKMOUSEEVENT *);
IMPORT HWND WINAPI SetCapture(HWND);
IMPORT BOOL WINAPI ReleaseCapture(void);
IMPORT BOOL WINAPI ScreenToClient(HWND, POINT *);
IMPORT int WINAPI GetSystemMetrics(int);
IMPORT BOOL WINAPI SetForegroundWindow(HWND);

IMPORT HDC WINAPI CreateCompatibleDC(HDC);
IMPORT HBITMAP WINAPI CreateCompatibleBitmap(HDC, int, int);
IMPORT HGDIOBJ WINAPI SelectObject(HDC, HGDIOBJ);
IMPORT BOOL WINAPI DeleteObject(HGDIOBJ);
IMPORT BOOL WINAPI DeleteDC(HDC);
IMPORT BOOL WINAPI BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD);
IMPORT HRGN WINAPI CreateRoundRectRgn(int, int, int, int, int, int);

IMPORT HINSTANCE WINAPI ShellExecuteW(HWND, const wchar_t *, const wchar_t *, const wchar_t *, const wchar_t *,
                                      int);
/* Imported by ordinal 12, present under that ordinal since Windows 2000 (by name only from Vista). */
IMPORT IStream *WINAPI SHCreateMemStream(const void *, UINT);

/* The executable's own module handle, provided by the linker (no GetModuleHandle needed). */
extern char __ImageBase;
#define UI_HINSTANCE ((HINSTANCE)&__ImageBase)

#endif
