/*
 * rt.h - Prototypes of the memory helpers in rt.c (the launcher links no C runtime).
 */
#ifndef LAUNCHER_RT_H
#define LAUNCHER_RT_H

#include <stddef.h>

void *memset(void *dst, int value, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);

#endif
