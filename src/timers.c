#define _GNU_SOURCE
#include "timers.h"
#include <stdio.h>
#include <time.h>
#include <stdbool.h>
#include <unistd.h>
#include "uvbloop.h"


#define MAXEVENTS 64


void *timer_loop(void *ptr) {
    uvbloop_event_t *events;
    if((events = calloc(MAXEVENTS, sizeof(uvbloop_event_t))) == NULL) {
        perror("calloc");
        return NULL;
    }
    timer_mgr_t *p = (timer_mgr_t *)ptr;
    int waiting;
    timer_entry_t *entry = NULL;
    while(true) {
        waiting = uvbloop_wait(p->loop, events, MAXEVENTS);
        if(waiting > 0) {
            for(int i=0; i<waiting; i++) {
                entry = (timer_entry_t *)uvbloop_event_data(events);
                uvbloop_reset_timer(p->loop, entry->id);
                if(entry->func(entry->data) < 0) {
                    perror("timer()");
                }
            }
        }

    }
}


int timer_mgr_init(timer_mgr_t *t) {
    pthread_mutex_init(&t->mutex, NULL);
    t->loop = NULL;
    RD_LIST_INIT(&t->funcs);
    if((t->loop = uvbloop_init(NULL)) == NULL) {
        perror("uvbloop_init");
        return -1;
    }
    if(pthread_create(&t->thread, NULL, timer_loop, (void *)t) != 0) {
        perror("pthread_create");
        return -1;
    }
    return 0;
}
static int32_t timer_id_filter(struct rd_list_node *node, void *cmpdata) {
    int id= *(int *)cmpdata;
    timer_entry_t *entry = RD_LIST_ENTRY(node, timer_entry_t);
    if(entry->id == id) {
        return 1;
    }
    return 0;
}


int register_timer(timer_mgr_t *t, timer_func_t func, uint64_t secs, void *data) {
    int ret = 0;

    timer_entry_t *entry = NULL;
    // set up the server sockets epoll event data
    if((entry = calloc(1, sizeof(timer_entry_t))) == NULL) {
        perror("malloc");
        goto register_timer_error;
    }

    entry->func = func;
    entry->secs= secs;
    entry->data = data;

    pthread_mutex_lock(&t->mutex);
    rd_list_append(&t->funcs, &entry->list);
    pthread_mutex_unlock(&t->mutex);

    if((entry->id = uvbloop_register_timer(t->loop, secs, entry)) == -1) {
        perror("uvbloop_register_timer");
        goto register_timer_error;
    }
    ret = entry->id;
    goto register_timer_ret;
register_timer_error:
    free(entry);
    ret = -1;
register_timer_ret:
    return ret;
}


int unregister_timer(timer_mgr_t *t, int id) {
    pthread_mutex_lock(&t->mutex);
    struct rd_list_node *node = rd_list_remove_by_func(&t->funcs, timer_id_filter, &id);
    pthread_mutex_unlock(&t->mutex);
    if(node == NULL) {
        return -1;
    }
    timer_entry_t *entry = RD_LIST_ENTRY(node, timer_entry_t);
    free(entry);
    return 0;
}
