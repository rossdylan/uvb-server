#include <stdio.h>
#include <pthread.h>
#include <stdint.h>
#include "counter.h"

#define NTHREADS 10
counter_t *counter;

void *inc(void *arg) {
    (void)arg;
    while (1) {
        counter_inc(counter, "robgssp");
    }

    return NULL;
}

void *watch(void *arg) {
    uint64_t last = 0;
    while(1) {
        uint64_t curr = counter_get(counter, "robgssp");
        printf("Count: %lu (%lu/s)\n", curr, curr - last);
        last = curr;
        sleep(1);
    }
}
                                          

int main() {
    counter = counter_init("./test.lmdb", NTHREADS);

    pthread_t threads[NTHREADS];
    pthread_t watcher;
    
    for (int i = 0; i < NTHREADS; ++i) {
        pthread_create(&threads[i], NULL, inc, NULL);
    }
    pthread_create(&watcher, NULL, watch, NULL);

    for (int i = 0; i < NTHREADS; ++i) {
        pthread_join(threads[i], NULL);
    }

    printf("Count: %lu\n", counter_get(counter, "robgssp"));
}
