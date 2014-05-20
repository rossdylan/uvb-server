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
    GQuark name_quark;
} Counter;

void new_namedb(NameDB* db, int fd, void* region, size_t size);
NameDB* load_names(size_t size);
void unload_names(NameDB* db);

void add_name(NameDB* db, const char* name);
uint64_t names_length(NameDB* db);
char** get_names(NameDB* db);
void free_names(char** names, uint64_t len);


void new_counterdb(CounterDB* db, int fd, void* region, size_t size, size_t cur_size);
void load_database(CounterDB* database, size_t size);
void unload_database(CounterDB* db);

Counter* add_counter(CounterDB* db, const char* name);
Counter* get_counter(CounterDB* db, const char* name);
void increment_counter(CounterDB* db, const char* name);
bool counter_exists(CounterDB* db, const char* name);
uint64_t num_counters(CounterDB* db);
char** counter_names(CounterDB* db);

/**
 * Load the names stored in the namesdb into the GHashTable
 */
void load_index(CounterDB* cdb);

/**
 * Expand the given file by 10 * _SC_PAGE_SIZE
 */
void expand_database(CounterDB* db);

/**
 * Get the size of a file
 */
off_t get_fsize(int fd);

/**
 * Truncate a file to the given size
 */
void truncate_file(int fd, off_t size);
#endif

