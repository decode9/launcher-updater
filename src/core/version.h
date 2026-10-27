/*
 * version.h - Launcher versions: strict X.Y.Z (decimal, no leading zeros except "0", each part
 * < 2^32). Used for the launcher's own version and the theme's min_launcher_version. Release
 * versions of the game are opaque strings and are only compared for equality.
 */
#ifndef LAUNCHER_CORE_VERSION_H
#define LAUNCHER_CORE_VERSION_H

#include <stdint.h>

typedef struct {
    uint32_t major, minor, patch;
} version;

int version_parse(const char *text, version *out); /* 1 on success */
int version_compare(const version *a, const version *b); /* -1, 0 or 1 */

/* Parses both and stores the comparison in *result; returns 0 if either is malformed. */
int version_compare_str(const char *a, const char *b, int *result);

#endif
