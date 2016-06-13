/**
 * File: lmdb_counter.c
 * Implementation of the LMDB counter. Persist uint64_t counters to disk using
 * LMDB. We explicity use MDB_WRITEMAP | MDB_MAPASYNC in order to get the speed
 * required for UVB. As such we do not have write durability. You have been
 * warned.
 */

#define _GNU_SOURCE
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <lmdb.h>
#include "counter.h"

struct counter {
    MDB_env *env;
    MDB_dbi *dbi;
};

// who the fuck knows why this number is chosen
// it was in an example.
#define MDB_MAPSIZE 10485760
#define MDB_CHECK(call, succ, ret) if((call) != succ) { perror(#call); return ret; }

counter_t *counter_init(const char *path, uint64_t readers) {
    counter_t *lc = NULL;
    if((lc = calloc(1, sizeof(counter_t))) == NULL) {
        perror("calloc");
        return NULL;
    }

    // Setup and open the lmdb enviornment
    MDB_CHECK(mdb_env_create(&lc->env), MDB_SUCCESS, NULL);
    MDB_CHECK(mdb_env_set_maxreaders(lc->env, readers), MDB_SUCCESS, NULL);
    MDB_CHECK(mdb_env_set_mapsize(lc->env, MDB_MAPSIZE), MDB_SUCCESS, NULL);
    MDB_CHECK(mdb_env_open(lc->env, path, MDB_WRITEMAP | MDB_MAPASYNC | MDB_NOSUBDIR, 0664), MDB_SUCCESS, NULL);

    MDB_txn *txn = NULL;
    MDB_CHECK(mdb_txn_begin(lc->env, NULL, 0, &txn), MDB_SUCCESS, NULL);

    if((lc->dbi = calloc(1, sizeof(MDB_dbi))) == NULL) {
        perror("calloc");
        return NULL;
    }

    MDB_CHECK(mdb_dbi_open(txn, NULL, 0, lc->dbi), MDB_SUCCESS, NULL);
    mdb_txn_commit(txn);
    return lc;
}


void counter_destroy(counter_t *lc) {
    mdb_dbi_close(lc->env, *lc->dbi);
    free(lc->dbi);
    mdb_env_close(lc->env);
    free(lc);
}


/**
 * Helper function for filtering out non-alphanumeric characters.
 */
static inline bool is_ascii(char c) {
    return (c > 47 && c < 58) || (c > 64 && c < 91) || (c > 96 && c < 123);
}


/**
 * This function works in stages. First we clean up the given key to prevent
 * any trickery by users. Then we retrieve the existing value, increment it,
 * and store it back in the db.
 */
uint64_t counter_inc(counter_t *lc, const char *key) {
    char clean_key[16]; // 15 characters + \0
    int clean_index = 0;
    for(int i=0; i<15; i++) {
        if(key[i] == '\0') {
            clean_key[clean_index] = '\0';
            break;
        }
        if(is_ascii(key[i])) {
            clean_key[clean_index] = key[i];
            clean_index++;
        }
    }
    clean_key[15] = '\0';

    MDB_val mkey, data, update;
    MDB_txn *txn = NULL;

    mkey.mv_size = (strlen(clean_key) + 1) * sizeof(char);
    mkey.mv_data = (void *)clean_key;

    // First we get our data from the db
    mdb_txn_begin(lc->env, NULL, 0, &txn);
    uint64_t stored_counter = 0;
    if(mdb_get(txn, *lc->dbi, &mkey, &data) == MDB_SUCCESS) {
        stored_counter = *(uint64_t *)data.mv_data;
    }
    stored_counter++;
    update.mv_size = sizeof(uint64_t);
    update.mv_data = (void *)&stored_counter;
    mdb_put(txn, *lc->dbi, &mkey, &update, 0);

    MDB_CHECK(mdb_txn_commit(txn), MDB_SUCCESS, 0);
    return stored_counter;
}


uint64_t counter_get(counter_t *lc, const char *key) {
    MDB_dbi dbi;
    MDB_val mkey, data;
    MDB_txn *txn;
    mdb_txn_begin(lc->env, NULL, 0, &txn);
    mdb_dbi_open(txn, NULL, 0, &dbi);

    mkey.mv_size = strlen(key) * sizeof(char);
    mkey.mv_data = (void *)key;

    // First we get our data from the db
    uint64_t stored_counter = 0;
    if(mdb_get(txn, dbi, &mkey, &data) == MDB_SUCCESS) {
        stored_counter = *(uint64_t *)data.mv_data;
    }
    mdb_dbi_close(lc->env, dbi);
    return stored_counter;
}


void counter_sync(counter_t *lc) {
    mdb_env_sync(lc->env, 1);
}


void counter_dump(counter_t *lc, buffer_t *output) {
    MDB_val key, data, rps_key, rps_data;
    MDB_txn *txn = NULL;
    MDB_cursor *cursor = NULL;
    mdb_txn_begin(lc->env, NULL, MDB_RDONLY, &txn);
    mdb_cursor_open(txn, *lc->dbi, &cursor);
    int rc = 0;
    char *tmp_str = NULL;
    int size = 0;
    uint64_t rps = 0;
    while((rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT)) == 0) {
        if((*(char *)key.mv_data) == '_') {
            continue;
        }
        size = asprintf(&tmp_str, "_%s_rps", (char *)key.mv_data);
        rps_key.mv_size = size;
        rps_key.mv_data = tmp_str;
        if(mdb_get(txn, *lc->dbi, &rps_key, &rps_data) == MDB_SUCCESS) {
            rps = *(uint64_t *)rps_data.mv_data;
        }
        free(tmp_str);
        size = asprintf(&tmp_str, "%s: %lu - %lurps\n", (char *)key.mv_data, *(uint64_t *)data.mv_data, rps);
        buffer_append(output, tmp_str, size);
        free(tmp_str);
    }
    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
}


int counter_gen_stats(void *tdata) {
    counter_t *lc = (counter_t *)tdata;
    MDB_val key, data;
    MDB_val stat_key, stat_data;
    MDB_txn *txn = NULL;
    MDB_cursor *cursor = NULL;
    mdb_txn_begin(lc->env, NULL, 0, &txn);
    mdb_cursor_open(txn, *lc->dbi, &cursor);
    int rc = 0;
    char *tmp_str = NULL;
    uint64_t last_counter = 0;
    uint64_t reqs_per_sec = 0;
    int size = 0;
    while((rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT)) == 0) {
        if((*(char *)key.mv_data) == '_') {
            continue;
        }
        size = asprintf(&tmp_str, "_%s_last", (char *)key.mv_data);
        stat_key.mv_size = size;
        stat_key.mv_data = tmp_str;
        last_counter = 0;
        if(mdb_get(txn, *lc->dbi, &stat_key, &stat_data) == MDB_SUCCESS) {
            last_counter = *(uint64_t *)stat_data.mv_data;
        }
        // runs every 10secs
        reqs_per_sec = (*(uint64_t *)data.mv_data - last_counter) / 10;
        stat_data.mv_size = sizeof(uint64_t);
        stat_data.mv_data = data.mv_data;
        mdb_put(txn, *lc->dbi, &stat_key, &data, 0);
        free(tmp_str);
        size = asprintf(&tmp_str, "_%s_rps", (char *)key.mv_data);
        stat_key.mv_size = size;
        stat_key.mv_data = tmp_str;
        stat_data.mv_size = sizeof(uint64_t);
        stat_data.mv_data = &reqs_per_sec;
        mdb_put(txn, *lc->dbi, &stat_key, &stat_data, 0);
        free(tmp_str);

    }
    mdb_cursor_close(cursor);
    mdb_txn_commit(txn);
    return 0;
}
