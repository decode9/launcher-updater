/*
 * thread.h - One worker thread and auto-reset events, for the window's worker (app/window.c).
 */
#ifndef LAUNCHER_THREAD_H
#define LAUNCHER_THREAD_H

#include <stdint.h>

typedef struct {
    void *handle;
} thread_handle;

typedef struct {
    void *handle;
} thread_event;

typedef void (*thread_fn)(void *arg);

/* Starts fn(arg) on a new thread. Returns 1, or 0 if the thread could not be created. */
int thread_start(thread_handle *t, thread_fn fn, void *arg);

/* Waits until the thread has returned and releases it. */
void thread_join(thread_handle *t);

/* Auto-reset event, initially not set. Returns 1 on success. */
int event_create(thread_event *e);
void event_set(thread_event *e);

/* Waits until the event is set or ms milliseconds have passed (0xFFFFFFFF = forever).
 * Returns 1 if the event was set, 0 on timeout. */
int event_wait(thread_event *e, uint32_t ms);

void thread_sleep(uint32_t ms);

#endif
