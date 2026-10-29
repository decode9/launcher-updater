/*
 * selfupdate.c - See selfupdate.h; app_swap is the --swap mode (step 4).
 *
 * Signer rule: an unsigned running executable (beta build) only relies on the SHA-256 from the
 * pinned status. A signed one requires that both executables verify (platform/win32/trust.h:
 * WinVerifyTrust with revocation of the whole chain checked online) and that the new signer's
 * subject and issuer names equal ours. Anything else refuses the update, including a running
 * executable that does not verify on this machine (revoked, tampered, revocation status
 * unavailable because the machine is offline, or Wine without chain building): the current
 * version keeps running and the update is retried on the next start.
 */
#include "selfupdate.h"
#include "app.h"
#include "app_internal.h"
#include "cache.h"
#include "core/sha256.h"
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
#define SWAP_LOCK_MS 10000u /* another launcher holding the folder after the old one exited */
#define LAUNCH_RETRIES 3u   /* a fresh executable may be held briefly by an antivirus scan */
#define LAUNCH_RETRY_MS 500u

static uint8_t g_exe[APP_BLOB_MAX];
static char g_self[FS_PATH_BYTES], g_new[FS_PATH_BYTES], g_target[FS_PATH_BYTES];
static trust_signer g_own_signer, g_new_signer;

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

/* SHA-256 of the running executable, read from disk once per run; 0 if unreadable. */
static const uint8_t *own_sha256(void)
{
    static uint8_t buf[0x10000], sha[SHA256_BYTES];
    static char name[FS_PATH_BYTES], path[FS_PATH_BYTES];
    static int state; /* 0 not read yet, 1 read, -1 unreadable */
    sha256_ctx ctx;
    fs_file f;
    size_t got = 0;
    int ok;
    if (state != 0) return state > 0 ? sha : 0;
    state = -1;
    if (fs_exe_name(name, sizeof name) && fs_join(path, sizeof path, app_dir, name) && fs_open_read(&f, path)) {
        sha256_init(&ctx);
        while ((ok = fs_read(&f, buf, sizeof buf, &got)) && got > 0) sha256_update(&ctx, buf, got);
        fs_close(&f);
        if (ok) {
            sha256_final(&ctx, sha);
            state = 1;
        }
    }
    if (state < 0) log_line("self-update: cannot read the running executable to compare it with the update");
    return state > 0 ? sha : 0;
}

/* The announced blob is the executable already running: a previous update was applied but the
 * published blob is not the announced version. Updating again would loop on every start. */
static int announced_blob_is_running(void)
{
    static int logged;
    const uint8_t *own = own_sha256();
    if (!own || !mem_eq(own, app_status.launcher.sha256, SHA256_BYTES)) return 0;
    if (!logged) {
        strbuf *sb = log_begin();
        sb_add(sb, "error: self-update: the server announces launcher ");
        sb_add(sb, app_status.launcher.version);
        sb_add(sb, " but its blob is this executable (");
        sb_add(sb, LAUNCHER_VERSION);
        sb_add(sb, "); the published version or blob is wrong, not updating");
        log_end();
        logged = 1;
    }
    return 1;
}

int selfupdate_available(void)
{
    int cmp;
    return app_status.launcher.present && version_compare_str(app_status.launcher.version, LAUNCHER_VERSION, &cmp) &&
           cmp > 0 && !announced_blob_is_running();
}

/* proc_launch, retried: an antivirus may hold a just-written executable for a moment. */
static int launch_retrying(const char *exe, const char *const *args, uint32_t argc)
{
    uint32_t i;
    for (i = 0;; i++) {
        if (proc_launch(exe, args, argc, app_dir)) return 1;
        if (i == LAUNCH_RETRIES) return 0;
        thread_sleep(LAUNCH_RETRY_MS);
    }
}

static void log_trust(const char *who, trust_result r, const trust_signer *signer)
{
    strbuf *sb = log_begin();
    sb_add(sb, "self-update: ");
    sb_add(sb, who);
    sb_add(sb, " executable ");
    sb_add(sb, trust_result_text(r));
    if (signer->subject[0]) {
        sb_add(sb, ", signer \"");
        sb_add(sb, signer->subject);
        sb_add(sb, "\"");
    }
    if (signer->issuer[0]) {
        sb_add(sb, ", issuer \"");
        sb_add(sb, signer->issuer);
        sb_add(sb, "\"");
    }
    log_end();
}

static int signer_accepted(void)
{
    trust_result own, next;
    if (!fs_exe_name(g_target, sizeof g_target) || !fs_join(g_self, sizeof g_self, app_dir, g_target)) return 0;
    own = trust_check(g_self, &g_own_signer);
    next = trust_check(g_new, &g_new_signer);
    log_trust("running", own, &g_own_signer);
    log_trust("new", next, &g_new_signer);
    /* Unsigned build: the SHA-256 from the pinned server is the only check (as for any blob).
     * Signed build: both must verify (revocation checked online; unknown status = not verified)
     * and the new signer must have the same subject and issuer. Otherwise we keep running the
     * current version. */
    if (own == TRUST_UNSIGNED) return 1;
    if (own != TRUST_VALID || next != TRUST_VALID) return 0;
    return str_eq(g_own_signer.subject, g_new_signer.subject) && str_eq(g_own_signer.issuer, g_new_signer.issuer);
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
        log_line("error: self-update: signature check failed (see above); keeping this version");
        fs_remove(g_new);
        return 0;
    }
    /* Closed meanwhile (the download and the signature check may take long): starting the new
     * executable would bring back the window the user just closed. The next start retries. */
    if (app_quitting()) {
        log_line("self-update: the launcher is closing; not starting the new executable");
        fs_remove(g_new);
        return 0;
    }
    u64_to_dec(proc_current_id(), pid);
    args[0] = "--swap";
    args[1] = pid;
    if (!launch_retrying(g_new, args, 2)) {
        log_line("error: self-update: cannot start the new executable; keeping this version");
        fs_remove(g_new);
        return 0;
    }
    log_line("self-update: new executable started, exiting");
    return 1;
}

/* <exe>.new.part: cache_write_atomic's temporary, left when the process ended while writing. */
static void remove_new_part(void)
{
    fs_file f;
    strbuf sb;
    sb_init(&sb, g_target, sizeof g_target);
    sb_add(&sb, g_new);
    sb_add(&sb, ".part");
    if (!sb.ok || !fs_open_read(&f, g_target)) return;
    fs_close(&f);
    if (fs_remove(g_target)) log_line("self-update: removed a leftover partial executable");
}

void selfupdate_cleanup(void)
{
    fs_file f;
    if (!new_path(g_new, sizeof g_new)) return;
    remove_new_part();
    if (!fs_open_read(&f, g_new)) return;
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
    /* Hold the folder while replacing, so no launcher started meanwhile runs from (or deletes)
     * the files being swapped; the old one has exited, so this only waits for a stray one. */
    if (app_lock(SWAP_LOCK_MS)) {
        for (i = 0; i < SWAP_RETRIES && !moved; i++) {
            moved = fs_rename(g_new, g_self);
            if (!moved) thread_sleep(SWAP_RETRY_MS);
        }
    }
    log_line(moved ? "swap: replaced the launcher " LAUNCHER_VERSION
                   : "error: swap: cannot replace the launcher; starting the old one");
    app_unlock(); /* before the launch: the started launcher must be able to take the folder */
    app_end();    /* the started launcher opens launcher.log itself */
    return proc_launch(g_self, args, 1, app_dir) ? 0 : 1;
}
