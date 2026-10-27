/*
 * thread.c - See thread.h.
 */
#include "thread.h"
#include "win32.h"

#define WAIT_OBJECT_0 0
#define INFINITE_WAIT 0xFFFFFFFFUL

typedef DWORD(WINAPI *thread_start_routine)(void *);

IMPORT HANDLE WINAPI CreateThread(void *, size_t, thread_start_routine, void *, DWORD, DWORD *);
IMPORT DWORD WINAPI WaitForSingleObject(HANDLE, DWORD);
IMPORT HANDLE WINAPI CreateEventW(void *, BOOL, BOOL, const wchar_t *);
IMPORT BOOL WINAPI SetEvent(HANDLE);
IMPORT void WINAPI Sleep(DWORD);

/* One worker at a time is all the launcher needs, so the trampoline's arguments are static. */
static thread_fn start_fn;
static void *start_arg;

static DWORD WINAPI trampoline(void *unused)
{
    (void)unused;
    start_fn(start_arg);
    return 0;
}

int thread_start(thread_handle *t, thread_fn fn, void *arg)
{
    start_fn = fn;
    start_arg = arg;
    t->handle = CreateThread(0, 0, trampoline, 0, 0, 0);
    return t->handle != 0;
}

void thread_join(thread_handle *t)
{
    if (!t->handle) return;
    WaitForSingleObject(t->handle, INFINITE_WAIT);
    CloseHandle(t->handle);
    t->handle = 0;
}

int event_create(thread_event *e)
{
    e->handle = CreateEventW(0, 0, 0, 0);
    return e->handle != 0;
}

void event_set(thread_event *e)
{
    if (e->handle) SetEvent(e->handle);
}

int event_wait(thread_event *e, uint32_t ms)
{
    return WaitForSingleObject(e->handle, (DWORD)ms) == WAIT_OBJECT_0;
}

void thread_sleep(uint32_t ms)
{
    Sleep((DWORD)ms);
}
