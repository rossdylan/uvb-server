/**
 * File: counter.h
 * Generic counter interface
 *
 * Provides interface for thread-safe counters as provided by lmdb or
 * our own TM implementation
 */
#pragma once

#include "buffer.h"

/**
 * Structure defining the global values required for the LMDB functions.
 */

typedef struct counter counter_t;

/**
 * Allocate and initialize a counter_t struct with the given path and
 * number of readers.
 */
counter_t *counter_init(const char *path, uint64_t readers);


/**
 * Deinitialize and free an counter_t structure
 */
void counter_destroy(counter_t *lc);


/**
 * Increment a counter with the given key. Disk write isn't implied.
 */
uint64_t counter_inc(counter_t *lc, const char *key);


/**
 * Return the value of the given counter
 */
uint64_t counter_get(counter_t *lc, const char *key);


/**
 * Force a dump to disk. May be slow.
 */
void counter_sync(counter_t *lc);


/**
 * Dump out the key/value pairs of counters within the database. It appends
 * the string representation of the kv pairs to the given buffer
 */
void counter_dump(counter_t *lc, buffer_t *buffer);

/**
 * Run via the timer system every 10 seconds to generate req/s statistics
 */
int counter_gen_stats(void *data);
