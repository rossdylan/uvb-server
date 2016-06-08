#include "pool.h"
#include <stdlib.h>
#include <stdio.h>


mempool_t *mempool_init(uint64_t obj_size, uint64_t obj_count) {
    mempool_t *new_pool = NULL;
    if((new_pool = malloc(sizeof(mempool_t))) == NULL) {
        perror("malloc");
        return NULL;
    }
    new_pool->obj_size = obj_size;
    new_pool->obj_count = obj_count;
    new_pool->free_map = NULL;
    if((new_pool->free_map = calloc(obj_count/8, sizeof(uint8_t))) == NULL) {
        perror("calloc");
        goto mempool_init_pool_err;
    }
    new_pool->objects = NULL;
    if((new_pool->objects = calloc(obj_count, obj_size)) == NULL) {
        perror("calloc");
        goto mempool_init_freemap_err;
    }
    goto mempool_init_ret;

mempool_init_freemap_err:
    free(new_pool->free_map);
mempool_init_pool_err:
    free(new_pool);
mempool_init_ret:
    return new_pool;
}


void mempool_destroy(mempool_t *pool) {
    free(pool->free_map);
    free(pool);
}


void *mempool_alloc(mempool_t *pool) {
    for(uint64_t i=0; i<pool->obj_count/8; i++) {
        for(int k=0;k<8;k++) {
            if(pool->free_map[i] & (1 << k)) {
                return (void *)(((uint64_t)pool->objects) + (uint64_t)((i+k) * pool->obj_size));
            }
        }
    }
    return NULL;
}


void mempool_free(mempool_t *pool, void *ptr) {
    uint64_t ptr_diff = ((uint64_t)ptr - (uint64_t)pool->objects) / pool->obj_size;
    uint64_t index = ptr_diff / 8;
    uint64_t byte_index = ptr_diff % 8;
    pool->free_map[index] ^= (1 << byte_index);
}
