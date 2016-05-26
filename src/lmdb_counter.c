#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include "lmdb_counter.h"


lmdb_counter_t *lmdb_counter_init(const char *path, uint64_t readers) {
    lmdb_counter_t *lc = NULL;
    if((lc = calloc(1, sizeof(lmdb_counter_t))) == NULL) {
        perror("calloc");
        return NULL;
    }
    if(mdb_env_create(&lc->env) != MDB_SUCCESS) {
        perror("mdb_env_create");
        return NULL;
    }
    if(mdb_env_set_maxreaders(lc->env, readers) != MDB_SUCCESS) {
        perror("mdb_env_set_maxreaders");
        return NULL;
    }

    // who the fuck knows why this number is chosen
    // it was in an example.
    if(mdb_env_set_mapsize(lc->env, 10485760) != MDB_SUCCESS) {
        perror("mdb_env_set_mapsize");
        return NULL;
    }
    if(mdb_env_open(lc->env, path, MDB_WRITEMAP | MDB_MAPASYNC | MDB_NOSUBDIR, 0664) != MDB_SUCCESS) {
        perror("mdb_env_open");
        return NULL;
    }
    MDB_txn *txn = NULL;
    if(mdb_txn_begin(lc->env, NULL, 0, &txn) != MDB_SUCCESS) {
        perror("mdb_txn_begin");
        return NULL;
    }

    if((lc->dbi = calloc(1, sizeof(MDB_dbi))) == NULL) {
        perror("calloc");
        return NULL;
    }
    if(mdb_dbi_open(txn, NULL, 0, lc->dbi) != MDB_SUCCESS) {
        perror("mdb_dbi_open");
        return NULL;
    }
    mdb_txn_commit(txn);
    return lc;
}


void lmdb_counter_destroy(lmdb_counter_t *lc) {
    mdb_dbi_close(lc->env, *lc->dbi);
    mdb_env_close(lc->env);
    free(lc);
}

static inline bool is_ascii(char c) {
    return (c > 47 && c < 58) || (c > 64 && c < 91) || (c > 96 && c < 123);
}


uint64_t lmdb_counter_inc(lmdb_counter_t *lc, const char *key) {
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
    if(mdb_txn_commit(txn) != MDB_SUCCESS) {
        perror("mdb_txn_commit");
        return 0;
    }
    return stored_counter;
}

uint64_t lmdb_counter_get(lmdb_counter_t *lc, const char *key) {
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


void lmdb_counter_sync(lmdb_counter_t *lc) {
    mdb_env_sync(lc->env, 1);
}

void lmdb_counter_dump(lmdb_counter_t *lc, buffer_t *output) {
    MDB_val key, data;
    MDB_txn *txn = NULL;
    MDB_cursor *cursor = NULL;
    mdb_txn_begin(lc->env, NULL, MDB_RDONLY, &txn);
    mdb_cursor_open(txn, *lc->dbi, &cursor);
    int rc = 0;
    char *tmp_str = NULL;
    while((rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT)) == 0) {
        int size = asprintf(&tmp_str, "%s: %lu\n", (char *)key.mv_data, *(uint64_t *)data.mv_data);
        buffer_append(output, tmp_str, size);
        free(tmp_str);
    }
    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
}
