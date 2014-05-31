#ifndef _UVB_UVBSTORE_H_
#define _UVB_UVBSTORE_H_

//glib has a ton of documentation warnings so we are going to ignore them and focus to make it easier
//to see our problems
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation"
#include <glib.h>
#pragma clang diagnostic pop

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

typedef struct {
    GHashTable* hashes;
    void* region;
    size_t max_size;
    size_t current_size;
    int fd;
} NameDB;

typedef struct {
    NameDB* names;
    GHashTable* index;
    GQueue* freespace_cache;
    size_t max_size;
    size_t current_size;
    void* region;
    int fd;
    bool gc_changed; // lets us know when the marker has run and found files to be marked
} CounterDB;

typedef struct {
    uint64_t number;
    uint64_t last_offset;
} DBHeader;

typedef struct {
    uint64_t count;
    uint64_t rps;
    uint64_t rps_prevcount;
    uint64_t name_hash;
    time_t last_updated;
    bool gc_flag;
} Counter;

/**
 * Header that goes before a name in the namesdb
 */
typedef struct {
    uint64_t name_size;
    bool gc_flag;
} NameHeader;

void namedb_new(NameDB* db, int fd, void* region, size_t size);
void namedb_load(NameDB* db, size_t size);
void namedb_unload(NameDB* db);

void namedb_add_name(NameDB* db, const char* name);
uint64_t namedb_length(NameDB* db);
char** namedb_get_names(NameDB* db);
char* namedb_name_from_hash(NameDB* db, uint64_t hash);
void namedb_expand(NameDB* db);

void counterdb_new(CounterDB* db, int fd, void* region, size_t size, size_t cur_size);
void counterdb_load(CounterDB* database, size_t size);
void counterdb_unload(CounterDB* db);

Counter* counterdb_add_counter(CounterDB* db, const char* name);
Counter* counterdb_get_counter(CounterDB* db, const char* name);
void counterdb_increment_counter(CounterDB* db, const char* name);
bool counterdb_counter_exists(CounterDB* db, const char* name);
uint64_t counterdb_length(CounterDB* db);
char** counterdb_get_names(CounterDB* db);
/**
 * Get an array of all counters stored in the db
 */
Counter** counterdb_get_counters(CounterDB* db);

/*
 * passed into g_hash_table_new_full to free the keys which are malloc'd ints
 */
void free_int_key(uint64_t* hash);
void free_string_key(char* hash);

/**
 * Wraps the calls to namedb_load and counterdb_load and returns a fully initialized counterdb
 */
CounterDB* init_database(size_t counters_size, size_t names_size);

/**
 * Load the names stored in the namesdb into the GHashTable
 */
void counterdb_load_index(CounterDB* cdb);

/**
 * Expand the given file by 10 * _SC_PAGE_SIZE
 */
void counterdb_expand(CounterDB* db);

/**
 * Get the size of a file
 */
off_t get_fsize(int fd);

/**
 * Truncate a file to the given size
 */
void truncate_file(int fd, off_t size);

/**
 * Take the given fd and truncate it to expand it by a predertimaned amount (10 pages)
 * and return its new size;
 */
uint64_t expand_file(int fd);

void counterdb_gc_mark(CounterDB* db);
void counterdb_fill_fsc(CounterDB* db);

/**
 * Rewrite the namedb file eliminating all tombstones
 */
void namedb_compact(NameDB* db);
#endif

