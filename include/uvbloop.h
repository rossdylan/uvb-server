#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * Abstract the options for READ/WRITE notifications
 * nset -> notification set
 */
typedef enum {
    UVBLOOP_R = 0x01,
    UVBLOOP_W = 0x02
} uvbloop_nset_t;

/**
 * Forward declare our structures
 */
typedef struct uvbloop uvbloop_t;

#ifdef EPOLL_BACKEND
#include <sys/epoll.h>
typedef struct epoll_event uvbloop_event_t;
#else
#include <stdint.h>
#include <sys/types.h>
#include <sys/event.h>
typedef struct kevent uvbloop_event_t;
#endif

/**
 * Create a new instance of uvbloop_t
 */
uvbloop_t *uvbloop_init(void *options);

/**
 * Register a file descriptor with the given uvbloop_t
 */
int uvbloop_register_fd(uvbloop_t *loop, int fd, void *data, uvbloop_nset_t nset);

/**
 * Unregister a file descriptor from the loop.
 */
int uvbloop_unregister_fd(uvbloop_t *loop, int fd);

/**
 * Register a timer with the given uvbloop_t.
 */
int uvbloop_register_timer(uvbloop_t *loop, uint64_t ms, void *data);

/**
 * Reset a timer after uvbloop_wait returns it's event
 */
int uvbloop_reset_timer(uvbloop_t *loop, int id);

/**
 * Unregister a timer from this eventloop. This is done by passing in an event
 */
int uvbloop_unregister_timer(uvbloop_t *loop, int id);

/**
 * Wait for events from the given uvbloop_t
 */
int uvbloop_wait(uvbloop_t *loop, uvbloop_event_t *events, uint64_t max_events);

/**
 * Check if an event has errors
 */
bool uvbloop_event_error(uvbloop_event_t *event);

/**
 * Get the data from an event
 */
void *uvbloop_event_data(uvbloop_event_t *event);

/**
 * Destroy an instance of uvbloop_t
 */
void uvbloop_destroy(uvbloop_t *loop);
