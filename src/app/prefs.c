/*
 * prefs.c - See prefs.h.
 */
#include "prefs.h"
#include "app_internal.h"
#include "cache.h"
#include "core/str.h"
#include "log.h"
#include "platform/win32/sys.h"

static int g_lock;
static char g_lang[PREFS_LANG_BYTES]; /* "" = no preference */
static int g_dirty;                   /* g_lang differs from the file */

/* A spin lock is enough: holders only copy a few bytes. */
static void lock(void)
{
    while (__atomic_exchange_n(&g_lock, 1, __ATOMIC_ACQUIRE)) __asm__ volatile("pause");
}

static void unlock(void)
{
    __atomic_store_n(&g_lock, 0, __ATOMIC_RELEASE);
}

static int prefs_path(char *out, size_t cap)
{
    return app_dir[0] && fs_join(out, cap, app_dir, PREFS_FILE);
}

void prefs_load(void)
{
    static char text[1024];
    static json_token tokens[64];
    char path[FS_PATH_BYTES], lang[PREFS_LANG_BYTES];
    size_t len;
    lang[0] = 0;
    /* Larger than any document the launcher writes: a bigger file is not ours, so ignored. */
    if (prefs_path(path, sizeof path) && fs_read_all(path, text, sizeof text, &len))
        prefs_parse_lang(text, len, tokens, sizeof tokens / sizeof tokens[0], lang);
    lock();
    str_copy(g_lang, sizeof g_lang, lang);
    g_dirty = 0;
    unlock();
}

void prefs_language(char *out, size_t cap)
{
    int ok;
    lock();
    ok = g_lang[0] && str_copy(out, cap, g_lang);
    unlock();
    if (!ok) sys_language(out, cap);
}

int prefs_set_language(const char *lang)
{
    if (!lang || (lang[0] && !prefs_lang_valid(lang))) return 0;
    lock();
    if (!str_eq(g_lang, lang)) {
        str_copy(g_lang, sizeof g_lang, lang);
        g_dirty = 1;
    }
    unlock();
    return 1;
}

int prefs_save(void)
{
    char path[FS_PATH_BYTES], doc[PREFS_DOC_BYTES], lang[PREFS_LANG_BYTES];
    size_t len;
    int dirty;
    strbuf *sb;
    lock();
    dirty = g_dirty;
    g_dirty = 0;
    str_copy(lang, sizeof lang, g_lang);
    unlock();
    if (!dirty) return 1;
    len = prefs_format(doc, sizeof doc, lang);
    if (len && prefs_path(path, sizeof path) && fs_make_parents(path) && cache_write_atomic(path, doc, len)) {
        sb = log_begin();
        sb_add(sb, "prefs: language ");
        sb_add(sb, lang[0] ? lang : "(system)");
        log_end();
        return 1;
    }
    log_line("error: cannot write " PREFS_FILE);
    lock();
    g_dirty = 1; /* tried again at the next save */
    unlock();
    return 0;
}
