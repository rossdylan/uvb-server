#include "uvbloop.h"
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


struct uvbloop {
    int epoll_fd;
};


/**
 * Allocate a new uvbloop structure and create a new epoll file descriptor
 */
uvbloop_t *uvbloop_init(void *options) {
    (void)options;
    uvbloop_t *new = NULL;
    if((new = malloc(sizeof(uvbloop_t))) == NULL) {
        perror("malloc");
        return NULL;
    }

    if((new->epoll_fd = epoll_create1(0)) == -1) {
        perror("epoll_create1");
        free(new);
        return NULL;
    }
    return new;
}


int uvbloop_register_fd(uvbloop_t *loop, int fd, void *data, uvbloop_nset_t nset) {
    char events = 0;
    if(nset & UVBLOOP_R) {
        events |= EPOLLIN;
    }
    if(nset & UVBLOOP_W) {
        events |= EPOLLOUT;
    }
    struct epoll_event event;
    event.data.ptr = data;
    if(epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1) {
        perror("epoll_ctl");
        return -1;
    }
    return 0;
}


/**
 * Register a timer with the given uvbloop_t.
 * Editors Note: Fuck Linux
 */
int uvbloop_register_timer(uvbloop_t *loop, uint64_t ms, void *data) {
    uint64_t ns = ms * 1000000;
    uint64_t sec = 0;
    struct itimerspec new_value;
    struct timespec now;
    int ret = 0;

    if(clock_gettime(CLOCK_REALTIME, &now) == -1) {
        perror("clock_gettime");
        return -1;
    }
    while(ns > 999999999) {
        sec += 1;
        ns -= 1000000000;
    }
    new_value.it_value.tv_sec = sec;
    new_value.it_value.tv_nsec = ns;
    new_value.it_interval.tv_sec = sec;
    new_value.it_interval.tv_nsec = ns;
    int fd = -1;
    if((fd = timerfd_create(CLOCK_MONOTONIC, 0)) == -1) {
        perror("timerfd_create");
        return -1;
    }

    if(timerfd_settime(fd, 0, &new_value, NULL) == -1) {
        perror("timerfd_settime");
        return -1;
    }

    if(uvbloop_register_fd(loop, fd, data, UVBLOOP_R) != 0) {
        perror("uvbloop_register_fd");
        return -1;
    }
    return fd;
}


/**
 * Unregister a timer from this eventloop. This is done by passing in an event
 */
int uvbloop_unregister_timer(uvbloop_t *loop, int id) {
    close(id);
    return uvbloop_unregister_fd(loop, id);
}


int uvbloop_reset_timer(uvbloop_t *loop, int id) {
    (void)loop;
    uint64_t exp;
    read(id, &exp, sizeof(uint64_t));
    (void)exp;
    return 0;
}


int uvbloop_unregister_fd(uvbloop_t *loop, int fd) {
    if(epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1) {
        perror("epoll_ctl");
        return -1;
    }
    return 0;
}


int uvbloop_wait(uvbloop_t *loop, uvbloop_event_t *events, uint64_t max_events) {
    return epoll_wait(loop->epoll_fd, (struct epoll_event *)events, max_events, -1);
}


bool uvbloop_event_error(uvbloop_event_t *event) {
    struct epoll_event e = *(struct epoll_event *)event;
    return e.events & EPOLLERR || e.events & EPOLLHUP || !(e.events & EPOLLIN);
}


void *uvbloop_event_data(uvbloop_event_t *event) {
    return ((struct epoll_event *)event)->data.ptr;
}


void uvbloop_destroy(uvbloop_t *loop) {
    close(loop->epoll_fd);
    free(loop);
}
