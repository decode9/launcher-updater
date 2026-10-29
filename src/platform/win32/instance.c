/*
 * instance.c - See instance.h.
 */
#include "instance.h"
#include "core/sha256.h"
#include "win32.h"

#define WAIT_OBJECT_0 0
#define WAIT_ABANDONED 0x80
#define NAME_PREFIX L"Local\\decode9-launcher-"

IMPORT HANDLE WINAPI CreateMutexW(void *, BOOL, const wchar_t *);
IMPORT BOOL WINAPI ReleaseMutex(HANDLE);
IMPORT DWORD WINAPI WaitForSingleObject(HANDLE, DWORD);

static HANDLE g_mutex;
static int g_owned;
static char g_key[INSTANCE_KEY_CHARS + 1];

static void make_key(const char *dir)
{
    static const char hex[] = "0123456789abcdef";
    sha256_ctx ctx;
    uint8_t digest[SHA256_BYTES];
    size_t i;
    sha256_init(&ctx);
    for (i = 0; dir[i]; i++) {
        char c = dir[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        sha256_update(&ctx, &c, 1);
    }
    sha256_final(&ctx, digest);
    for (i = 0; i < INSTANCE_KEY_CHARS / 2; i++) {
        g_key[2 * i] = hex[digest[i] >> 4];
        g_key[2 * i + 1] = hex[digest[i] & 15];
    }
    g_key[INSTANCE_KEY_CHARS] = 0;
}

instance_result instance_acquire(const char *dir, uint32_t wait_ms)
{
    wchar_t name[sizeof NAME_PREFIX / sizeof(wchar_t) + INSTANCE_KEY_CHARS];
    size_t n = sizeof NAME_PREFIX / sizeof(wchar_t) - 1, i;
    DWORD wait;
    if (g_owned) return INSTANCE_OK;
    make_key(dir);
    for (i = 0; i < n; i++) name[i] = NAME_PREFIX[i];
    for (i = 0; i < INSTANCE_KEY_CHARS; i++) name[n + i] = (wchar_t)g_key[i];
    name[n + INSTANCE_KEY_CHARS] = 0;
    /* Not owned at creation: ownership comes only from the wait, so a holder that crashed
     * (WAIT_ABANDONED) hands it over and an existing but released mutex is free. */
    g_mutex = CreateMutexW(0, 0, name);
    if (!g_mutex) return INSTANCE_ERROR;
    wait = WaitForSingleObject(g_mutex, (DWORD)wait_ms);
    if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) {
        g_owned = 1;
        return INSTANCE_OK;
    }
    CloseHandle(g_mutex);
    g_mutex = 0;
    return INSTANCE_BUSY;
}

const char *instance_key(void)
{
    return g_key;
}

void instance_release(void)
{
    if (g_owned) ReleaseMutex(g_mutex);
    if (g_mutex) CloseHandle(g_mutex);
    g_mutex = 0;
    g_owned = 0;
}
