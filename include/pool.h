#pragma once
#include <stdint.h>

/**
 * Structure of a memory pool.
 * Pools keep track of the object size, the number of objects,
 * and a bitmap of which objects are free. This is not thread safe
 * so each thread should have their own instance.
 */
typedef struct {
    uint64_t obj_size;
    uint64_t obj_count;
    uint8_t *free_map;
    void *objects;
} mempool_t;


mempool_t *mempool_init(uint64_t obj_size, uint64_t obj_count);

void mempool_destroy(mempool_t *pool);

void *mempool_alloc(mempool_t *pool);

void mempool_free(mempool_t *pool, void *ptr);
