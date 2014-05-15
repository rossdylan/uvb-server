#ifndef _UVB_UVBSTORE_H_
#define _UVB_UVBSTORE_H_

#include <glib.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int fd;
    uint64_t size;
    uint8_t* region;
} NameDB;

typedef struct {
    int fd;
    uint8_t* region;
    uint64_t max_size;
    uint64_t current_size;
    NameDB* names;
    GHashTable* index;
} CounterDB;


typedef struct {
    uint64_t number;
    uint64_t last_offset;
} DBHeader;

typedef struct {
    uint64_t count;
    GQuark name_quark;
} Counter;

void new_namedb(NameDB* db, int fd, uint8_t* region, uint64_t size);
NameDB* load_names(uint64_t size);
void unload_names(NameDB* db);

void add_name(NameDB* db, const char* name);
int names_length(NameDB* db);
char** get_names(NameDB* db);
void free_names(char** names, int len);


void new_counterdb(CounterDB* db, int fd, uint8_t* region, uint64_t size, uint64_t cur_size);
CounterDB* load_database(uint64_t size);
void unload_database(CounterDB* db);

Counter* add_counter(CounterDB* db, const char* name);
Counter* get_counter(CounterDB* db, const char* name);
void increment_counter(CounterDB* db, const char* name);
bool counter_exists(CounterDB* db, const char* name);

void load_index(CounterDB* cdb);
#endif

