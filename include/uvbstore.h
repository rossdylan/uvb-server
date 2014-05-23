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
    void* region;
    size_t max_size;
    size_t current_size;
    int fd;
} NameDB;

typedef struct {
    NameDB* names;
    GHashTable* index;
    size_t max_size;
    size_t current_size;
    void* region;
    int fd;
} CounterDB;


typedef struct {
    uint64_t number;
    uint64_t last_offset;
} DBHeader;

typedef struct {
    uint64_t count;
    uint64_t rps;
    uint64_t rps_prevcount;
    GQuark name_quark;
} Counter;

void namedb_new(NameDB* db, int fd, void* region, size_t size);
NameDB* namedb_load(size_t size);
void namedb_unload(NameDB* db);

void namedb_add_name(NameDB* db, const char* name);
uint64_t namedb_length(NameDB* db);
char** namedb_get_names(NameDB* db);
void free_names(char** names, uint64_t len);


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
#endif

