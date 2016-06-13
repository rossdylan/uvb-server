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

#define KEYSZ 16

static const unsigned char zero_key[KEYSZ] = { 0 };

struct hashslot {
    unsigned char key[KEYSZ];
    uint64_t count;
};

struct counter {
    size_t size;
    size_t used;
    struct hashslot *slots;
};

static const int size0 = 128;

counter_t *counter_init(const char *path, uint64_t threads) {
    (void)path; (void)threads;
    struct counter *tbl = malloc(sizeof(struct counter));
    tbl->size = size0;
    tbl->slots = calloc(size0, sizeof(struct hashslot));
    tbl->used = 0;
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
    free(tbl->slots);
    free(tbl);
}

static inline uint64_t key_incr(size_t tblsz,
                                struct hashslot *slots,
                                const unsigned char *key,
                                uint64_t count) {
    for (size_t i = hash(key) % tblsz;; i = (i + 1) % tblsz) {
        if (memcmp(key, &slots[i].key, KEYSZ) == 0) {
            uint64_t old = slots[i].count;
            slots[i].count += count;
            return old;
        } else if (memcmp(zero_key, &slots[i].key, KEYSZ) == 0) {
            memcpy(&slots[i].key, key, KEYSZ);
            slots[i].count = count;
            return 0;
        }
    }
}

/**
 * Helper function for filtering out non-alphanumeric characters.
 */
static inline bool is_ascii(char c) {
    return (c > 47 && c < 58) || (c > 64 && c < 91) || (c > 96 && c < 123);
}

static inline void key_clean(unsigned char *dest, const char *src) {
    int clean_index = 0;
    for(int i=0; i < KEYSZ-1; i++) {
        if(src[i] == '\0') {
             break;
        }
        if(is_ascii(src[i])) {
            dest[clean_index] = src[i];
            clean_index++;
        }
    }
}
    
uint64_t counter_inc(counter_t *tbl, const char *key) {
    unsigned char clean_key[KEYSZ] = { 0 }; // 15 characters + \0
    key_clean(clean_key, key);

    // bump the table size if necessary
    __transaction_relaxed {
        tbl->used += 1;
        if (tbl->used > tbl->size * 8 / 10) {
            size_t newsize = tbl->size * 2;
            struct hashslot *newslots = calloc(tbl->size, sizeof(struct hashslot));

            for (size_t i = 0; i < tbl->size; ++i) {
                if (memcmp(tbl->slots[i].key, zero_key, KEYSZ) != 0) {
                    key_incr(newsize, newslots, tbl->slots[i].key, tbl->slots[i].count);
                }
            }
            tbl->size = newsize;
            tbl->slots = newslots;
        }
    }

    __transaction_relaxed {
        return key_incr(tbl->size, tbl->slots, clean_key, 1);
    }
}

uint64_t counter_get(counter_t *tbl, const char *key) {
    unsigned char clean_key[KEYSZ] = { 0 };
    key_clean(clean_key, key);
    __transaction_relaxed {
        return key_incr(tbl->size, tbl->slots, clean_key, 0);
    }
}

void counter_dump(counter_t *tbl, buffer_t *output) {
    __transaction_relaxed {
        for (size_t i = 0; i < tbl->size; ++i) {
            if (memcmp(tbl->slots[i].key, zero_key, KEYSZ) == 0) {
                char *str = NULL;
                size_t size = asprintf(&str, "%.*s: %lu\n",
                                       KEYSZ,
                                       tbl->slots[i].key,
                                       tbl->slots[i].count);
                buffer_append(output, str, size);
                free(str);
            }
        }
    }
}

int counter_gen_stats(void *data) {
    (void)data;
    /* __transaction_relaxed { */
    /*     for (int i = 0; i < tbl->size; ++i) { */
            
    /* } */
    return 0;
}


