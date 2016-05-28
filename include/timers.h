/**
 * File: timers.h
 * Define a system for periodically running tasks within UVB. This is done
 * using timerfd's and epoll.
 */
#pragma once

#include <pthread.h>
#include "list.h"
#include "stdint.h"


typedef int (*timer_func_t)(void *data);


/**
 * Structure defining a function to be executed periodically by the event loop
 */
typedef struct {
    timer_func_t func;
    int tfd;
    uint64_t secs;
    void *data;
    struct list_node list;
} timer_entry_t;


/**
 * Structure storing the global state of the timer system. Things like a mutex
 * for concurrent access from within the UVB threadpool.
 *
 */
typedef struct {
    struct list_head funcs;
    pthread_mutex_t mutex;
    pthread_t thread;
    int epoll_fd;
} timer_mgr_t;


/**
 * Prepare the periodicals struct. Return -1 if something fails.
 */
int timer_mgr_init(timer_mgr_t *p);


/**
 * Register a new function to be run after the given number of seconds. Returns
 * -1 on failure, the timer file descriptor on success.
 */
int register_timer(timer_mgr_t *p, timer_func_t func, uint64_t secs, void *data);


/**
 * Unregister the timer given by its FD. Which acts like an ID. Return 0 on
 * success and -1 if the given timer doesn't exist.
 */
