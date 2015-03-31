#include "counter.h"
#include <stdlib.h>
#include <stdio.h>

void thread_counter_init(thread_counter_t *counter) {
    counter->counter = 0;
    if((counter->lock = malloc(sizeof(pthread_rwlock_t))) == NULL) {
        perror("malloc");
        //urp$
    }
    pthread_rwlock_init(counter->lock, NULL);
}

global_counter_t *new_global_counter(uint64_t n) {
    global_counter_t *gcounter = NULL;
    if((gcounter = malloc(sizeof(global_counter_t))) == NULL) {
        perror("malloc");
        //herp?
    }
    gcounter->ncounters = n;
    if((gcounter->counters = calloc(n, sizeof(thread_counter_t *))) == NULL) {
        perror("calloc");
        //lerp...
    }
    for(uint64_t i=0; i<n; i++) {
        if((gcounter->counters[i] = malloc(sizeof(thread_counter_t))) == NULL) {
            perror("malloc");
            //ferp!
        }
        thread_counter_init(gcounter->counters[i]);
    }
    return gcounter;
}

void counter_inc(global_counter_t *gc, uint64_t thread) {
    pthread_rwlock_wrlock(gc->counters[thread]->lock);
    gc->counters[thread]->counter++;
    pthread_rwlock_unlock(gc->counters[thread]->lock);
}

uint64_t global_counter_value(global_counter_t *gc) {
    uint64_t counter = 0;
    for(uint64_t i=0; i<gc->ncounters; i++) {
        pthread_rwlock_rdlock(gc->counters[i]->lock);
        counter += gc->counters[i]->counter;
        pthread_rwlock_unlock(gc->counters[i]->lock);
    }
    return counter;
}

void global_counter_free(global_counter_t *gc) {
    for(uint64_t i=0; i<gc->ncounters; i++) {
        pthread_rwlock_destroy(gc->counters[i]->lock);
    }
    free(gc->counters);
    free(gc);
}

