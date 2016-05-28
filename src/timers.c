#include "timers.h"
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <time.h>
#include <stdbool.h>
#include <unistd.h>


#define MAXEVENTS 64


void *timer_loop(void *ptr) {
    struct epoll_event *events;
    if((events = calloc(MAXEVENTS, sizeof(struct epoll_event))) == NULL) {
        perror("calloc");
        return NULL;
    }
    timer_mgr_t *p = (timer_mgr_t *)ptr;
    int waiting;
    timer_entry_t *entry = NULL;
    uint64_t exp;
    while(true) {
        waiting = epoll_wait(p->epoll_fd, events, MAXEVENTS, -1);
        if(waiting > 0) {
            for(int i=0; i<waiting; i++) {
                entry = (timer_entry_t *)events[i].data.ptr;
                read(entry->tfd, &exp, sizeof(uint64_t));
                if(entry->func(entry->data) < 0) {
                    perror("timer()");
                }
            }
        }

    }
}


int timer_mgr_init(timer_mgr_t *t) {
    pthread_mutex_init(&t->mutex, NULL);
    t->epoll_fd = -1;
    LIST_INIT(&t->funcs)
    if((t->epoll_fd = epoll_create1(0)) == -1) {
        perror("epoll_create");
        return -1;
    }
    if(pthread_create(&t->thread, NULL, timer_loop, (void *)t) != 0) {
        perror("pthread_create");
        return -1;
    }
    return 0;
}
static int32_t timer_fd_filter(struct list_node *node, void *cmpdata) {
    int tfd = *(int *)cmpdata;
    timer_entry_t *entry = LIST_ENTRY(node, timer_entry_t);
    if(entry->tfd == tfd) {
        return 1;
    }
    return 0;
}




int register_timer(timer_mgr_t *t, timer_func_t func, uint64_t secs, void *data) {
    struct itimerspec new_value;
    struct timespec now;
    struct epoll_event event;
    int ret = 0;

    // set up the server sockets epoll event data
    if((event.data.ptr = calloc(1, sizeof(timer_entry_t))) == NULL) {
        perror("malloc");
        event.data.ptr = NULL;
        goto register_timer_error;
    }
    timer_entry_t *entry = (timer_entry_t *)event.data.ptr;

    entry->func = func;
    entry->secs= secs;
    entry->data = data;

    if(clock_gettime(CLOCK_REALTIME, &now) == -1) {
        perror("clock_gettime");
        goto register_timer_error;
    }

    new_value.it_value.tv_sec = now.tv_sec + secs;
    new_value.it_value.tv_nsec = now.tv_nsec;
    new_value.it_interval.tv_sec = secs;
    new_value.it_interval.tv_nsec = 0;
    if((entry->tfd = timerfd_create(CLOCK_REALTIME, 0)) == -1) {
        perror("timerfd_create");
        goto register_timer_error;
    }
    if(timerfd_settime(entry->tfd, TFD_TIMER_ABSTIME, &new_value, NULL) == -1) {
        perror("timerfd_settime");
        goto register_timer_error;
    }

    pthread_mutex_lock(&t->mutex);
    list_append(&t->funcs, &entry->list);
    pthread_mutex_unlock(&t->mutex);

    event.events = EPOLLIN;
    if(epoll_ctl(t->epoll_fd, EPOLL_CTL_ADD, entry->tfd, &event) != 0) {
        perror("epoll_ctl");
        goto register_timer_error;
    }
    ret = entry->tfd;
    goto register_timer_ret;
register_timer_error:
    free(event.data.ptr);
    ret = -1;
register_timer_ret:
    return ret;
}


int unregister_timer(timer_mgr_t *t, int fd) {
    pthread_mutex_lock(&t->mutex);
    struct list_node *node = list_remove_by_func(&t->funcs, timer_fd_filter, &fd);
    pthread_mutex_unlock(&t->mutex);
    if(node == NULL) {
        return -1;
    }
    timer_entry_t *entry = LIST_ENTRY(node, timer_entry_t);
    close(entry->tfd);
    free(entry);
    return 0;
}
