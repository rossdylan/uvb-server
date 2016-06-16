/**
 * File: atomic_counter.c
 *
 * A thread-safe hash table counter implementation, written atomically
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <assert.h>
#include <err.h>
#include "counter.h"
#include "server.h"

#define atomic_load_relaxed(X) (atomic_load_explicit(X, memory_order_relaxed))
#define atomic_fetch_add_relaxed(X, i) (atomic_fetch_add_explicit(X, i, memory_order_relaxed))
#define atomic_compare_exchange_relaxed(obj, exp, des)           \
    (atomic_compare_exchange_strong_explicit(obj, exp, des,        \
                                             memory_order_relaxed, \
                                             memory_order_relaxed))

#define KEYSZ 16

typedef struct {
    unsigned char chars[KEYSZ];
} hashkey_t;

static const hashkey_t zero_key = { .chars = { 0 } };

struct hashslot {
    _Atomic hashkey_t key;
    _Atomic uint64_t count;
};

struct counter {
    size_t size;
    _Atomic size_t used;
    struct hashslot *slots;
    _Atomic(struct counter *) prev, prev2;
};

static const int size0 = 128;

counter_t *counter_init(const char *path, uint64_t threads) {
    (void)path; (void)threads;
    assert(atomic_is_lock_free((_Atomic hashkey_t*)NULL));
    struct counter *tbl = malloc(sizeof(struct counter));
    tbl->size = size0;
    tbl->slots = calloc(size0, sizeof(struct hashslot));
    tbl->used = 0;
    tbl->prev = tbl->prev2 = NULL;
    return tbl;
}

/* djb2 hash
 */
size_t hash(const hashkey_t key) {
    size_t hash = 5381;

    int c, i = 0;
    while ((c = key.chars[i++])) {
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
    struct hashslot *slots = tbl->slots;

    for (size_t i = 0; i < tbl1->size; ++i) {
        tbl1->slots[i].key = slots[i].key;
        tbl1->slots[i].count = slots[i].count;
    }

    return tbl1;
}

static inline bool key_eq(hashkey_t key1, hashkey_t key2) {
    return memcmp(&key1, &key2, sizeof(key1)) == 0;
}

static uint64_t key_incr(counter_t *tbl,
                                const hashkey_t key,
                                uint64_t count) {
    size_t size = tbl->size;

    for (size_t i = hash(key) % size;; i = (i + 1) % size) {
        hashkey_t key1 = atomic_load_relaxed(&tbl->slots[i].key);

        if (key_eq(key1, key)) {
            return atomic_fetch_add_relaxed(&tbl->slots[i].count, count);
        } else if (key_eq(key1, zero_key)) {
            hashkey_t key2 = zero_key;
            atomic_compare_exchange_relaxed(&tbl->slots[i].key, &key2, key);
            if (key_eq(key2, key) || key_eq(key2, zero_key)) {
                size_t used = atomic_fetch_add_relaxed(&tbl->used, 1);
                if (used + 1 > (size * 8) / 10) {
                    errx(1, "Hash table filled up");
                }
                return atomic_fetch_add_relaxed(&tbl->slots[i].count, count);
            }
        }
    }
}

static inline uint64_t key_get(counter_t *tbl, const hashkey_t key) {
    size_t size = tbl->size;
    for (size_t i = hash(key) % size;; i = (i + 1) % size) {
        hashkey_t key1 = atomic_load_relaxed(&tbl->slots[i].key);

        if (key_eq(key, key1)) {
            return atomic_load_relaxed(&tbl->slots[i].count);
        } else if (key_eq(key, zero_key)) {
            return 0;
        }
    }
}

static inline hashkey_t key_clean1(const char *src) {
    hashkey_t ret = { .chars = { 0 } };
    int clean_index = 0;
    for(int i=0; i < KEYSZ-1; i++) {
        if(src[i] == '\0') {
             break;
        }
        if(is_ascii(src[i])) {
            ret.chars[clean_index] = src[i];
            clean_index++;
        }
    }
    return ret;
}

uint64_t counter_inc(counter_t *tbl, const char *key) {
    hashkey_t clean_key = key_clean1(key);

    return key_incr(tbl, clean_key, 1);
}

uint64_t counter_get(counter_t *tbl, const char *key) {
    hashkey_t clean_key = key_clean1(key);

    return key_get(tbl, clean_key);
}

void counter_dump(counter_t *tbl, buffer_t *output) {
    size_t size = atomic_load_relaxed(&tbl->size);
    counter_t *prev = atomic_load_relaxed(&tbl->prev);
    counter_t *prev2 = atomic_load_relaxed(&tbl->prev2);

    for (size_t i = 0; i < size; ++i) {
        hashkey_t key = atomic_load_relaxed(&tbl->slots[i].key);
        if (!key_eq(tbl->slots[i].key, zero_key)) {
            uint64_t count = atomic_load_relaxed(&tbl->slots[i].count);
            char *str = NULL;
                
            uint64_t prevc = prev != NULL ? key_get(prev, key) : 0;
            uint64_t prevc2 = prev2 != NULL ? key_get(prev2, key) : 0;
            uint64_t rate = (prevc - prevc2) / STATS_SECS;

            size_t size = asprintf(&str, "%.*s: %lu - %lurps\n",
                                   KEYSZ,
                                   key.chars,
                                   count,
                                   rate);
            buffer_append(output, str, size);
            free(str);
        }
    }
}

int counter_gen_stats(void *data) {
    counter_t *tbl = data;
    counter_destroy(tbl->prev2);
    counter_t *prev = atomic_load_relaxed(&tbl->prev);
    atomic_store(&tbl->prev2, prev);
    atomic_store(&tbl->prev, counter_copy(tbl));
    return 0;
}
