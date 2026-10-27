/*
 * fs.h - Minimal file access with UTF-8 paths (converted to UTF-16 for the W APIs).
 * Paths are native (absolute or relative to the current directory); fs_join builds one from
 * the launcher folder and a spec path ('/' becomes '\'). The caller validates spec paths with
 * core/path.h first.
 */
#ifndef LAUNCHER_FS_H
#define LAUNCHER_FS_H

#include <stddef.h>
#include <stdint.h>

#define FS_PATH_BYTES 1024

typedef struct {
    void *handle;
} fs_file;

/* Folder of the running executable, without a trailing separator. */
int fs_exe_dir(char *out, size_t cap);

/* out = dir + '\' + rel with '/' turned into '\'. */
int fs_join(char *out, size_t cap, const char *dir, const char *rel);

/* Reads a whole file into buf; fails if it is larger than cap. */
int fs_read_all(const char *path, void *buf, size_t cap, size_t *len);

int fs_open_read(fs_file *f, const char *path);
int fs_open_append(fs_file *f, const char *path); /* creates it if missing */
int fs_create(fs_file *f, const char *path);      /* truncates */
int fs_read(fs_file *f, void *buf, size_t cap, size_t *got); /* got = 0 at end of file */
int fs_write(fs_file *f, const void *data, size_t size);
int fs_size(fs_file *f, uint64_t *size);
int fs_flush(fs_file *f);
int fs_open_rw(fs_file *f, const char *path);      /* read/write, created if missing, kept */
int fs_seek(fs_file *f, uint64_t offset);
int fs_truncate(fs_file *f, uint64_t size);       /* end of file at size */

/* File name of the running executable (e.g. "launcher.exe"). */
int fs_exe_name(char *out, size_t cap);
int fs_rename(const char *from, const char *to);  /* atomic replace, write-through */
int fs_make_parents(const char *path);            /* creates every folder above path */
int fs_remove(const char *path);                  /* 1 also if it did not exist */
int fs_free_space(const char *dir, uint64_t *bytes);
/* Not reentrant: fs_rename and fs_make_parents use static UTF-16 buffers. */
void fs_close(fs_file *f);

#endif
