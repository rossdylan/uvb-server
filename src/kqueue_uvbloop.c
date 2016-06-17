#include "uvbloop.h"
#include <stdint.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>


#define KQ_MAX_CL_SIZE 64


struct uvbloop {
    int kq_fd;
    int cl_index;
    struct kevent pending[KQ_MAX_CL_SIZE];
};


uvbloop_t *uvbloop_init(void *options) {
    (void)options;
    uvbloop_t *new = NULL;
    if((new = malloc(sizeof(uvbloop_t))) == NULL) {
        perror("malloc");
        return NULL;
    }
    if((new->kq_fd = kqueue()) == -1) {
        perror("kqueue");
        free(new);
        return NULL;
    }
    new->cl_index = 0;
    return new;
}


int uvbloop_register_fd(uvbloop_t *loop, int fd, void *data, uvbloop_nset_t nset) {
    if(loop->cl_index == KQ_MAX_CL_SIZE) {
        const struct kevent *pending = loop->pending;
        int res = kevent(loop->kq_fd, pending, loop->cl_index, NULL, 0, NULL);
        if(res == -1) {
            perror("kevent");
            return -1;
        }
        loop->cl_index = 0;
    }
    short filter = 0;
    if(nset & UVBLOOP_R) {
        filter |= EVFILT_READ;
    }
    if(nset & UVBLOOP_W) {
        filter |= EVFILT_WRITE;
    }
    EV_SET(&loop->pending[loop->cl_index], (uintptr_t)fd, filter, EV_ADD, 0, 0, data);
    loop->cl_index++;
    return 0;
}


/**
 * Register a timer using the timer system that is a part of kqueue. In this case
 * we explicitly call kevent after creating the timer to ensure it starts
 * immediately.
 */
int uvbloop_register_timer(uvbloop_t *loop, uint64_t ms, void *data) {
    short filter = EVFILT_TIMER;
    short flags = EV_ADD;
    short fflags = 0;
    EV_SET(&loop->pending[loop->cl_index], (uintptr_t)data, filter, flags, fflags, ms, data);
    loop->cl_index++;
    const struct kevent *pending = loop->pending;
    int res = kevent(loop->kq_fd, pending, loop->cl_index, NULL, 0, NULL);
    if(res == -1) {
        perror("kevent");
        return -1;
    }
    loop->cl_index = 0;
    return (uintptr_t)data;
}


/**
 * the unregister_timer function looks a lot like the register timer function!
 * This is because kqueue uses the same syscall for pretty much everything.
 */
int uvbloop_unregister_timer(uvbloop_t *loop, int id) {
    short filter = 0;
    short fflags = 0;
    short flags = EV_DELETE;
    EV_SET(&loop->pending[loop->cl_index], (uintptr_t)id, filter, flags, fflags, 0, NULL);
    loop->cl_index++;
    const struct kevent *pending = loop->pending;
    int res = kevent(loop->kq_fd, pending, loop->cl_index, NULL, 0, NULL);
    if(res == -1) {
        perror("kevent");
        return -1;
    }
    loop->cl_index = 0;
    return 0;
}


int uvbloop_reset_timer(uvbloop_t *loop, int id) {
    (void)loop;
    (void)id;
    return 0;
}


int uvbloop_unregister_fd(uvbloop_t *loop, int fd) {
    if(loop->cl_index == KQ_MAX_CL_SIZE) {
        const struct kevent *pending = loop->pending;
        int res = kevent(loop->kq_fd, pending, loop->cl_index, NULL, 0, NULL);
        if(res == -1) {
            perror("kevent");
            return -1;
        }
        loop->cl_index = 0;
    }
    short filter = 0;
    short fflags = 0;
    short flags = EV_DELETE;
    EV_SET(&loop->pending[loop->cl_index], (uintptr_t)fd, filter, flags, fflags, 0, NULL);
    loop->cl_index++;
    return 0;
}


int uvbloop_wait(uvbloop_t *loop, uvbloop_event_t *events, uint64_t max_events) {
    const struct kevent *pending = loop->pending;
    int res = kevent(loop->kq_fd, pending, loop->cl_index,
            (struct kevent *)events, max_events, NULL);
    loop->cl_index = 0;
    return res;
}


bool uvbloop_event_error(uvbloop_event_t *event) {
    return ((struct kevent *)event)->flags & EV_ERROR;
}


void *uvbloop_event_data(uvbloop_event_t *event) {
    return ((struct kevent *)event)->udata;
}


void uvbloop_destroy(uvbloop_t *loop) {
    close(loop->kq_fd);
    free(loop);
}
