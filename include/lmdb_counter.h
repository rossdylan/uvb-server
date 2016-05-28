/**
 * File: lmdb_counter.h
 * Headers for the LMDB counter implementation. Increment/Return a uin64_t and
 * persist it to disk. LMDB is great for this since it is fast, and supports
 * threading by default.
 */
#pragma once

#include <lmdb.h>
#include <stdint.h>
#include "buffer.h"


// who the fuck knows why this number is chosen
// it was in an example.
#define MDB_MAPSIZE 10485760
#define MDB_CHECK(call, succ, ret) if((call) != succ) { perror(#call); return ret; }


/**
 * Structure defining the global values required for the LMDB functions.
 */
typedef struct {
    MDB_env *env;
    MDB_dbi *dbi;
} lmdb_counter_t;


/**
 * Allocate and initialize a lmdb_counter_t struct with the given path and
 * number of readers.
 */
lmdb_counter_t *lmdb_counter_init(const char *path, uint64_t readers);


/**
 * Deinitialize and free an lmdb_counter_t structure
 */
void lmdb_counter_destroy(lmdb_counter_t *lc);


/**
 * Increment a counter with the given key. This is done within a lmdb
 * transaction, BUT we do not ensure msync has been called. This is for speed.
 * lmdb_counter_sync is required to ensure the counter has been persisted to
 * disk.
 */
uint64_t lmdb_counter_inc(lmdb_counter_t *lc, const char *key);


/**
 * Return the value of the given counter
 */
uint64_t lmdb_counter_get(lmdb_counter_t *lc, const char *key);


/**
 * Force LMDB to msync its mmap'd memory to disk.
 * This is slow...
 */
void lmdb_counter_sync(lmdb_counter_t *lc);


/**
 * Dump out the key/value pairs of counters within the database. It appends
 * the string representation of the kv pairs to the given buffer
 */
void lmdb_counter_dump(lmdb_counter_t *lc, buffer_t *buffer);

/**
 * Run via the timer system every 10 seconds to generate req/s statistics
 */
int lmdb_counter_gen_stats(void *data);
