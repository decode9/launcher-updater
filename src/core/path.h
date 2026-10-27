/*
 * path.h - Confinement rule for every path that comes from configuration or a manifest.
 *
 * A safe path is relative to the launcher folder, uses '/' as separator and cannot leave it:
 * no root or drive, no '\', no ':', no empty, '.' or '..' component, no control characters, no
 * component ending in '.' or ' ' (Windows strips them) and no reserved device name (CON, NUL,
 * COM1...). The spec (docs/spec.md) rejects a whole manifest if any path breaks the rule.
 */
#ifndef LAUNCHER_CORE_PATH_H
#define LAUNCHER_CORE_PATH_H

#define PATH_MAX_BYTES 260

int path_is_safe_relative(const char *path);

#endif
