/*
 * dir.h - Lists the entries of a folder (UTF-8 paths), for tools such as the theme preview.
 */
#ifndef LAUNCHER_DIR_H
#define LAUNCHER_DIR_H

/* Called once per entry ("." and ".." skipped) with the full path dir + '\' + name. Return 0
 * to stop the listing. */
typedef int (*dir_entry_fn)(void *ctx, const char *path, int is_dir);

/* Returns 1 if the folder could be read (even if empty), 0 otherwise. */
int dir_list(const char *dir, dir_entry_fn fn, void *ctx);

#endif
