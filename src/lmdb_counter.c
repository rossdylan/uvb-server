#include "lmdb_counter.h"
#include "stdlib.h"
#include "stdio.h"
#include <string.h>


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
    if(mdb_env_open(lc->env, path, MDB_FIXEDMAP | MDB_NOSUBDIR, 0664) != MDB_SUCCESS) {
        perror("mdb_env_open");
        return NULL;
    }
    return lc;
}


void lmdb_counter_destroy(lmdb_counter_t *lc) {
    mdb_env_close(lc->env);
    free(lc);
}


uint64_t lmdb_counter_inc(lmdb_counter_t *lc, const char *key) {
    MDB_dbi dbi;
    MDB_val mkey, data, update;
    MDB_txn *txn = NULL;

    mdb_txn_begin(lc->env, NULL, 0, &txn);

    mdb_dbi_open(txn, NULL, 0, &dbi);

    mkey.mv_size = strlen(key) * sizeof(char);
    mkey.mv_data = (void *)key;

    // First we get our data from the db
    uint64_t stored_counter = 0;
    if(mdb_get(txn, dbi, &mkey, &data) == MDB_SUCCESS) {
        stored_counter = *(uint64_t *)data.mv_data;
    }
    stored_counter++;
    update.mv_size = sizeof(uint64_t);
    update.mv_data = (void *)&stored_counter;
    mdb_put(txn, dbi, &mkey, &update, 0);
    if(mdb_txn_commit(txn) != MDB_SUCCESS) {
        perror("mdb_txn_commit");
        return 0;
    }
    mdb_dbi_close(lc->env, dbi);
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
