/**
 * File: tm_counter.c
 *
 * A thread-safe hash table counter implementation, written with
 * transactional memory
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "counter.h"
#include "server.h"


static const unsigned char zero_key[KEYSZ] = { 0 };

struct hashslot {
    unsigned char key[KEYSZ];
    uint64_t count;
};

struct counter {
    size_t size;
    size_t used;
    struct hashslot *slots;
    struct counter *prev, *prev2;
};

static const int size0 = 128;

counter_t *counter_init(const char *path, uint64_t threads) {
    (void)path; (void)threads;
    struct counter *tbl = malloc(sizeof(struct counter));
    tbl->size = size0;
    tbl->slots = calloc(size0, sizeof(struct hashslot));
    tbl->used = 0;
    tbl->prev = tbl->prev2 = NULL;
    return tbl;
}

/* djb2 hash
 */
size_t hash(const unsigned char *key) {
    size_t hash = 5381;

    int c;
    while ((c = *key++)) {
        hash = (hash * 33) ^ c;
    }
    return hash;
}

void counter_destroy(counter_t *tbl) {
    if (tbl != NULL) {
        free(tbl->slots);
        free(tbl);
    }
}

counter_t *counter_copy(counter_t *tbl) {
    counter_t *tbl1 = malloc(sizeof(counter_t));
    tbl1->size = tbl->size;
    tbl1->used = tbl->used;
    tbl1->slots = malloc(tbl1->size * sizeof(struct hashslot));
    memcpy(tbl1->slots, tbl->slots, tbl1->size * sizeof(struct hashslot));
    return tbl1;
}

static void table_expand(counter_t *tbl);

static inline uint64_t key_incr0(counter_t *tbl,
                                 const unsigned char *key,
                                 uint64_t count) {
    for (size_t i = hash(key) % tbl->size;; i = (i + 1) % tbl->size) {
        if (memcmp(key, &tbl->slots[i].key, KEYSZ) == 0) {
            uint64_t old = tbl->slots[i].count;
            tbl->slots[i].count += count;
            return old;
        } else if (memcmp(zero_key, &tbl->slots[i].key, KEYSZ) == 0) {
            memcpy(&tbl->slots[i].key, key, KEYSZ);
            tbl->slots[i].count = count;
            tbl->used += 1;
            return 0;
        }
    }
}

static inline uint64_t key_incr(counter_t *tbl,
                                const unsigned char *key,
                                uint64_t count) {
    uint64_t res = key_incr0(tbl, key, count);
    if (tbl->used > (tbl->size * 8) / 10) {
        table_expand(tbl);
    }
    return res;
}

static void table_expand(counter_t *tbl) {
    printf("Expanding table...\n");
    size_t oldsize = tbl->size;
    tbl->size *= 2;
    struct hashslot *oldslots = tbl->slots;
    tbl->slots = calloc(tbl->size, sizeof(struct hashslot));

    for (size_t i = 0; i < oldsize; ++i) {
        if (memcmp(oldslots[i].key, zero_key, KEYSZ) != 0) {
            key_incr0(tbl, oldslots[i].key, oldslots[i].count);
        }
    }
}

static inline uint64_t key_get(counter_t *tbl,
                               const unsigned char *key) {
    for (size_t i = hash(key) % tbl->size;; i = (i + 1) % tbl->size) {
        if (memcmp(key, &tbl->slots[i].key, KEYSZ) == 0) {
            return tbl->slots[i].count;
        } else if (memcmp(zero_key, &tbl->slots[i].key, KEYSZ) == 0) {
            return 0;
        }
    }
}

uint64_t counter_inc(counter_t *tbl, const char *key) {
    unsigned char clean_key[KEYSZ] = { 0 }; // 15 characters + \0
    key_clean(clean_key, key);

    __transaction_relaxed {
        return key_incr(tbl, clean_key, 1);
    }
}

uint64_t counter_get(counter_t *tbl, const char *key) {
    unsigned char clean_key[KEYSZ] = { 0 };
    key_clean(clean_key, key);

    __transaction_relaxed {
        return key_get(tbl, clean_key);
    }
}

void counter_dump(counter_t *tbl, buffer_t *output) {
    __transaction_relaxed {
        for (size_t i = 0; i < tbl->size; ++i) {
            if (memcmp(tbl->slots[i].key, zero_key, KEYSZ) != 0) {
                char *str = NULL;

                uint64_t prev = tbl->prev != NULL ? key_get(tbl->prev, tbl->slots[i].key) : 0;
                uint64_t prev2 = tbl->prev2 != NULL ? key_get(tbl->prev2, tbl->slots[i].key) : 0;
                uint64_t rate = (prev - prev2) / STATS_SECS;

                size_t size = asprintf(&str, "%.*s: %lu - %lurps\n",
                                       KEYSZ,
                                       tbl->slots[i].key,
                                       tbl->slots[i].count,
                                       rate);
                buffer_append(output, str, size);
                free(str);
            }
        }
    }
}

int counter_gen_stats(void *data) {
    __transaction_relaxed {
        counter_t *tbl = data;
        counter_destroy(tbl->prev2);
        tbl->prev2 = tbl->prev;
        tbl->prev = counter_copy(tbl);
        return 0;
    }
}
