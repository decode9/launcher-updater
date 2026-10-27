/*
 * selfupdate.c - See selfupdate.h; app_swap is the --swap mode (step 4).
 *
 * Signer rule: if the running executable is Authenticode-signed and Windows accepts the
 * signature, the new one must be accepted too and have the same signer subject. If the running
 * one is signed but the signature does not verify on this machine (for example under Wine, see
 * platform/win32/trust.h), the new one must still carry a signature with the same subject. An
 * unsigned build (beta) only relies on the SHA-256 from the pinned status.
 */
#include "selfupdate.h"
#include "app.h"
#include "app_internal.h"
#include "cache.h"
#include "core/str.h"
#include "core/version.h"
#include "log.h"
#include "platform/win32/proc.h"
#include "platform/win32/sys.h"
#include "platform/win32/thread.h"
#include "platform/win32/trust.h"

#define NEW_SUFFIX ".new"
#define SWAP_WAIT_MS 60000u
#define SWAP_RETRIES 40u
#define SWAP_RETRY_MS 250u
#define SUBJECT_BYTES 256

static uint8_t g_exe[APP_BLOB_MAX];
static char g_self[FS_PATH_BYTES], g_new[FS_PATH_BYTES], g_target[FS_PATH_BYTES];
static char g_own_subject[SUBJECT_BYTES], g_new_subject[SUBJECT_BYTES];

static int new_path(char *out, size_t cap)
{
    char name[FS_PATH_BYTES];
    strbuf sb;
    if (!fs_exe_name(name, sizeof name)) return 0;
    sb_init(&sb, out, cap);
    sb_add(&sb, app_dir);
    sb_add_char(&sb, '\\');
    sb_add(&sb, name);
    sb_add(&sb, NEW_SUFFIX);
    return sb.ok;
}

int selfupdate_available(void)
{
    int cmp;
    return app_status.launcher.present && version_compare_str(app_status.launcher.version, LAUNCHER_VERSION, &cmp) &&
           cmp > 0;
}

static void log_trust(const char *who, trust_result r, const char *subject)
{
    strbuf *sb = log_begin();
    sb_add(sb, "self-update: ");
    sb_add(sb, who);
    sb_add(sb, " executable ");
    sb_add(sb, trust_result_text(r));
    if (subject[0]) {
        sb_add(sb, ", signer \"");
        sb_add(sb, subject);
        sb_add(sb, "\"");
    }
    log_end();
}

static int signer_accepted(void)
{
    trust_result own, next;
    if (!fs_exe_name(g_target, sizeof g_target) || !fs_join(g_self, sizeof g_self, app_dir, g_target)) return 0;
    own = trust_check(g_self, g_own_subject, sizeof g_own_subject);
    next = trust_check(g_new, g_new_subject, sizeof g_new_subject);
    log_trust("running", own, g_own_subject);
    log_trust("new", next, g_new_subject);
    if (own == TRUST_UNSIGNED) return 1;
    if (next == TRUST_UNSIGNED || !g_own_subject[0] || !str_eq(g_own_subject, g_new_subject)) return 0;
    return own == TRUST_INVALID || next == TRUST_VALID;
}

int selfupdate_start(void)
{
    const status_blob *b = &app_status.launcher;
    char pid[24];
    const char *args[2];
    strbuf *sb = log_begin();
    sb_add(sb, "self-update: ");
    sb_add(sb, LAUNCHER_VERSION);
    sb_add(sb, " -> ");
    sb_add(sb, b->version);
    sb_add(sb, ", ");
    sb_add_u64(sb, b->size);
    sb_add(sb, " bytes");
    log_end();
    if (b->size == 0 || b->size > sizeof g_exe || !app_fetch_blob(b->sha256, b->size, g_exe, sizeof g_exe, 0)) {
        log_line("error: self-update: download failed or SHA-256 mismatch; keeping this version");
        return 0;
    }
    if (!new_path(g_new, sizeof g_new) || !cache_write_atomic(g_new, g_exe, (size_t)b->size)) {
        log_line("error: self-update: cannot write the new executable; keeping this version");
        return 0;
    }
    if (!signer_accepted()) {
        log_line("error: self-update: the new executable's signer does not match; keeping this version");
        fs_remove(g_new);
        return 0;
    }
    u64_to_dec(proc_current_id(), pid);
    args[0] = "--swap";
    args[1] = pid;
    if (!proc_launch(g_new, args, 2, app_dir)) {
        log_line("error: self-update: cannot start the new executable; keeping this version");
        fs_remove(g_new);
        return 0;
    }
    log_line("self-update: new executable started, exiting");
    return 1;
}

void selfupdate_cleanup(void)
{
    fs_file f;
    if (!new_path(g_new, sizeof g_new) || !fs_open_read(&f, g_new)) return;
    fs_close(&f);
    log_line(fs_remove(g_new) ? "self-update: removed a leftover new executable"
                              : "self-update: cannot remove a leftover new executable");
}

/* ---- --swap <pid> ---- */

/* g_target = own path without ".new" (ASCII case-insensitive suffix). */
static int swap_target(void)
{
    static char own[FS_PATH_BYTES];
    size_t n, k = sizeof NEW_SUFFIX - 1, i;
    if (!fs_exe_name(own, sizeof own) || !str_copy(g_target, sizeof g_target, own)) return 0;
    n = str_len(g_target);
    if (n <= k) return 0;
    for (i = 0; i < k; i++) {
        char c = g_target[n - k + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != NEW_SUFFIX[i]) return 0;
    }
    g_target[n - k] = 0;
    return fs_join(g_self, sizeof g_self, app_dir, g_target) && fs_join(g_new, sizeof g_new, app_dir, own);
}

int app_swap(void)
{
    static const char *const args[1] = {"--skip-update"};
    char pid_text[24];
    uint64_t pid = 0;
    uint32_t i;
    int exited, moved = 0;
    if (!sys_flag_value("--swap", pid_text, sizeof pid_text) || !dec_to_u64(pid_text, str_len(pid_text), &pid) ||
        pid > 0xFFFFFFFFu)
        return 1;
    /* Wait first: the old process holds launcher.log open until it exits. */
    exited = proc_wait_exit((uint32_t)pid, SWAP_WAIT_MS);
    if (!app_begin("--swap")) return 1;
    if (!exited) log_line("error: swap: the old launcher did not exit in time");
    if (!swap_target()) {
        log_line("error: swap: this executable is not named <launcher>" NEW_SUFFIX);
        app_end();
        return 1;
    }
    for (i = 0; i < SWAP_RETRIES && !moved; i++) {
        moved = fs_rename(g_new, g_self);
        if (!moved) thread_sleep(SWAP_RETRY_MS);
    }
    log_line(moved ? "swap: replaced the launcher " LAUNCHER_VERSION
                   : "error: swap: cannot replace the launcher; starting the old one");
    app_end(); /* the started launcher opens launcher.log itself */
    return proc_launch(g_self, args, 1, app_dir) ? 0 : 1;
}
