#pragma once

#include <lmdb.h>
#include <stdint.h>
#include "buffer.h"

typedef struct {
    MDB_env *env;
    MDB_dbi *dbi;
} lmdb_counter_t;


lmdb_counter_t *lmdb_counter_init(const char *path, uint64_t readers);
void lmdb_counter_destroy(lmdb_counter_t *lc);
uint64_t lmdb_counter_inc(lmdb_counter_t *lc, const char *key);
uint64_t lmdb_counter_get(lmdb_counter_t *lc, const char *key);
void lmdb_counter_sync(lmdb_counter_t *lc);
void lmdb_counter_dump(lmdb_counter_t *lc, buffer_t *buffer);