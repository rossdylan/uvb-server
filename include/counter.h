#pragma once
#include <pthread.h>
#include <stdint.h>


typedef struct {
    uint64_t counter;
    pthread_rwlock_t *lock;
} thread_counter_t;


typedef struct {
    uint64_t ncounters;
    thread_counter_t **counters;
} global_counter_t;

void thread_counter_init(thread_counter_t *counter);
global_counter_t *new_global_counter(uint64_t n);
void counter_inc(global_counter_t *tc, uint64_t thread);
uint64_t global_counter_value(global_counter_t *gc);
void global_counter_free(global_counter_t *gc);
