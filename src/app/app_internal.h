/*
 * app_internal.h - State and steps shared by the launcher's modes (app.c: --status and
 * --install; window.c: the window; selfupdate.c: self-update and --swap). Not reentrant: the
 * window's worker thread is the only caller while the window is open.
 */
#ifndef LAUNCHER_APP_INTERNAL_H
#define LAUNCHER_APP_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "core/config.h"
#include "core/install.h"
#include "core/status.h"
#include "platform/win32/fs.h"

#ifndef LAUNCHER_VERSION
#error "build.sh passes -DLAUNCHER_VERSION from the VERSION file"
#endif

#define APP_CONFIG_FILE "launcher.json"
#define APP_LOG_FILE "launcher.log"
#define APP_STATUS_MAX 65536
#define APP_BLOB_MAX (8u << 20) /* largest blob kept in memory: images, fonts, the launcher */

extern char app_dir[FS_PATH_BYTES];      /* launcher folder (native, no trailing separator) */
extern char app_exe_name[FS_PATH_BYTES]; /* file name of the running executable */
extern launcher_config app_config;
extern launcher_status app_status;

/* Finds the launcher folder, opens launcher.log and writes "launcher <version> <mode>".
 * Returns 0 if the folder is unknown (nothing can work then). */
int app_begin(const char *mode);
void app_end(void);

int app_load_config(void);

/* GET /v1/status into app_status (logged). The raw document stays available until the next
 * fetch through app_status_text, so it can be cached. */
int app_fetch_status(void);
const char *app_status_text(size_t *len);

/* Parses a cached status document into app_status (not logged). */
int app_parse_status(const char *text, size_t len);

/* Executable name and install ports (core/install.h), once. */
int app_ports_init(void);

/* Downloads a whole blob into buf and checks its SHA-256 (and its size, unless size is
 * APP_SIZE_UNKNOWN: theme and news images are referenced by hash only). *len (may be 0)
 * receives the length. Returns 1 if it matches. */
#define APP_SIZE_UNKNOWN UINT64_MAX
int app_fetch_blob(const uint8_t sha[SHA256_BYTES], uint64_t size, void *buf, size_t cap, size_t *len);

/* 1 if state.json says app_status.release is installed and complete. */
int app_release_is_current(void);
int app_install_interrupted(void); /* an install started and did not finish */

/* Installs app_status.release (manifest, then the engine), logging. progress may be 0.
 * Returns 0 installed or already current, 1 configuration error, 2 network/server error,
 * 3 install error (*result holds the details when not 0). */
int app_install_release(install_progress_fn progress, void *user, volatile const int *cancel,
                        install_result **result);

/* Logs install progress (each phase/file change and about every eighth of a download). */
void app_log_progress(const install_progress *p);

#endif
