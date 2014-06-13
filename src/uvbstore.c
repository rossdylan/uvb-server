#include "uvbstore.h"
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>

/**
 * Initialize a new CounterDB by memsetting it to 0 and setting all its values
 */
void counterdb_new(CounterDB* db, int fd, void* region, size_t size, size_t cur_size, const char* fname) {
    db->fd = fd;
    db->region = region;
    db->max_size = size;
    db->current_size = cur_size;
    db->index = g_hash_table_new_full(g_str_hash, g_str_equal, (GDestroyNotify)free_string_key, NULL);
    db->names = NULL;
    db->gc_changed = true;
    db->freespace_cache = g_queue_new();
    db->fname = fname;
}

off_t get_fsize(int fd) {
    struct stat stats;
    if(fstat(fd, &stats) == -1) {
        perror("fstat: get_fsize");
        exit(EXIT_FAILURE);
    }
    return stats.st_size;
}

void truncate_file(int fd, off_t size) {
    do {
        if(errno == EINTR) {
            errno = 0;
        }
        if(ftruncate(fd, size) == -1) {
            if(errno != EINTR) {
                perror("ftruncate: expand_database");
                exit(EXIT_FAILURE);
            }
        }
    } while(errno == EINTR);
}

uint64_t expand_file(int fd) {
    off_t incr = 10 * sysconf(_SC_PAGE_SIZE);
    off_t cur_size = get_fsize(fd);
    off_t new_size = cur_size + incr;
    if(new_size < 0) {
        fprintf(stderr, "The namesdb new size is negative, aborting\n");
        exit(EXIT_FAILURE);
    }
    truncate_file(fd, new_size);
    return (uint64_t)new_size;
}

void namedb_expand(NameDB* db) {
    uint64_t new_size = expand_file(db->fd);
    const char* fname = db->fname;
    namedb_unload(db);
    namedb_load(db, fname, new_size);
}

void counterdb_expand(CounterDB* db) {
    uint64_t new_size = expand_file(db->fd);
    NameDB* names = db->names;
    const char* fname = db->fname;
    counterdb_unload(db);
    counterdb_load(db, fname, new_size);
    db->names = names;
    counterdb_load_index(db);
    counterdb_gc_mark(db);
    counterdb_fill_fsc(db);
}

CounterDB* init_database(size_t counters_size, size_t names_size, const char* counter_fname, const char* name_fname) {
    NameDB* names = malloc(sizeof(NameDB));
    CounterDB* counters = malloc(sizeof(CounterDB));

    namedb_load(names, name_fname, names_size);
    counterdb_load(counters, counter_fname, counters_size);
    counters->names = names;
    counterdb_load_index(counters);
    counterdb_gc_mark(counters);
    counterdb_fill_fsc(counters);
    fprintf(stderr, "Loaded %lu names\n", namedb_length(names));
    fprintf(stderr, "Loaded %lu counters\n", counterdb_length(counters));
    return counters;
}

/**
 * Load in a database of the given size
 * TODO(rossdylan) verify that the size given is the correct size, if not,
 * remap with more space
 */
void counterdb_load(CounterDB* database, const char* fname, size_t size) {
    int fd;
    do {
        if(errno == EINTR)
            errno = 0;
        if((fd = open(fname, O_CREAT | O_RDWR, S_IRWXU)) == -1) {
            if(errno != EINTR) {
                perror("open: load_database");
                exit(EXIT_FAILURE);
            }
        }
    } while(errno == EINTR);

    off_t fsize = get_fsize(fd);
    bool empty = false;
    if (fsize == 0) {
        empty = true;
        if(size > INT64_MAX) {
            fprintf(stderr, "CounterDB is too large, can't load\n");
            exit(EXIT_FAILURE);
        }
        truncate_file(fd, (off_t)size);
    }
    if(fsize < 0) {
        fprintf(stderr, "CounterDB is negative... exiting\n");
        exit(EXIT_FAILURE);
    }
    if((size_t)fsize > size) {
        size = (size_t)fsize;
    }
    void* region;
    if((region = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
        perror("mmap: CounterDB Region");
        exit(EXIT_FAILURE);
    }
    DBHeader* header = (DBHeader* )region;
    if (empty) {
        memset(header, 0, sizeof(DBHeader));
        header->number = 0;
        header->last_offset = sizeof(DBHeader);
    }
    size_t current_size = sizeof(DBHeader) + sizeof(Counter) * header->number;
    counterdb_new(database, fd, region, size, current_size, fname);
}

/**
 * Scan the names.db and counters.db to rebuild the in memory
 * index (GHashTable)
 */
void counterdb_load_index(CounterDB* db) {
    Counter* current;
    DBHeader* header = (DBHeader* )db->region;
    for (uint64_t index = 0; index < header->number; ++index) {
        current = (Counter* )((void* )((char* )db->region + sizeof(DBHeader) + (index * sizeof(Counter)) + 1));
        const char* name = namedb_name_from_hash(db->names, current->name_hash);
        if(name == NULL) {
            // we found a counter without a name.. probably a corrupted db
            continue;
        }
        char* memName = calloc(strlen(name) + 1, sizeof(char));
        memmove(memName, name, (strlen(name) + 1) * sizeof(char));
        g_hash_table_insert(db->index, (gpointer)memName, current);
    }
}

/**
 * Destroy a CounterDB struct:
 * - unmap the memory region
 * - close the file descriptor
 * - destroy the GHashTable
 */
void counterdb_unload(CounterDB* db) {
    if (munmap(db->region, db->max_size) == -1) {
        perror("munmap");
        abort();
    }
    do {
        if(errno == EINTR)
            errno = 0;
        if(close(db->fd) == -1) {
            if(errno != EINTR) {
                perror("close: unload_database");
                exit(EXIT_FAILURE);
            }
        }
    } while(errno == EINTR);
    g_hash_table_destroy(db->index);
    g_queue_free(db->freespace_cache);
}

/**
 * Add a new counter to the database:
 * - write new counter to counters.db
 * - update counters.db metadata (offset/num counters)
 * - add name to the names.db
 * - add name -> Counter* mapping to the index
 *   XXX(rossdylan) Add a check to make sure we don't overrun the end of our DB
 */
Counter* counterdb_add_counter(CounterDB* db, const char* name) {
    DBHeader* header = (DBHeader* )db->region;
    if((db->current_size + sizeof(Counter)) >= db->max_size || (char*)db->region+header->last_offset+1 >= (char*)db->region+db->max_size) {
        counterdb_expand(db);
        fprintf(stderr, "expanded counterdb: cur_size=%lu max_size=%lu\n", db->current_size, db->max_size);
    }
    bool used_cache = false;
    header = (DBHeader* )db->region;
    if(db->gc_changed && g_queue_get_length(db->freespace_cache) == 0) {
        counterdb_fill_fsc(db);
        db->gc_changed = false;
    }
    Counter* new_counter = NULL;
    if(g_queue_get_length(db->freespace_cache) == 0) {
        new_counter = (Counter* )((void* )((char* )db->region + header->last_offset + 1));
    }
    else {
        new_counter = (Counter* )g_queue_pop_head(db->freespace_cache);
        // this new counter was originally an old counter remove it from our index
        const char* old_name = namedb_name_from_hash(db->names, new_counter->name_hash);
        if(old_name != NULL) {
            if(counterdb_counter_exists(db, old_name)) {
                g_hash_table_remove(db->index, old_name);
            }
        }
        used_cache = true;
    }
    if(db->current_size + sizeof(Counter) >= db->max_size) {
        fprintf(stderr, "Shit son, you way to big: %p\n", new_counter);
    }
    memset(new_counter, 0, sizeof(Counter));
    new_counter->count = 0;
    new_counter->rps = 0;
    new_counter->rps_prevcount = 0;
    new_counter->name_hash = g_str_hash(name);
    new_counter->last_updated = time(NULL); //TODO check rval
    header->number++;
    header->last_offset = header->last_offset + sizeof(Counter);
    size_t nlen = (strlen(name) + 1);
    char* localname = calloc(nlen, sizeof(char));
    memmove(localname, name, sizeof(char) * nlen);
    g_hash_table_insert(db->index, (gpointer)localname, new_counter);
    if(!used_cache) {
        db->current_size += sizeof(Counter);
    }
    namedb_add_name(db->names, name);
    fprintf(stderr, "Created counter for %s. used_cache=%d\n", name, used_cache);
    return new_counter;
}

/**
 * check if a counter exists.
 * This doesn't go to disk through the mmap'd file, instead it just
 * checks the index. This is fine because during normal run time everything on
 * disk is fully mirrored into index.
 */
bool counterdb_counter_exists(CounterDB* db, const char* name) {
    if(GLIB_CHECK_VERSION(2, 3, 2)) {
        return g_hash_table_contains(db->index, name);
    }
    else {
        return g_hash_table_lookup(db->index, name) != NULL;
    }
}

/**
 * Return a pointer to a counter
 */
Counter* counterdb_get_counter(CounterDB* db, const char* name) {
    Counter* counter = g_hash_table_lookup(db->index, (gpointer)name);
    return counter;
}

/**
 * Increment the given counter by 1
 */
void counterdb_increment_counter(CounterDB* db, const char* name) {
    counterdb_get_counter(db, name)->count++;
    time_t cur_time;
    if((cur_time = time(NULL)) == -1) {
        perror("time: increment_counter");
        exit(EXIT_FAILURE);
    }
    counterdb_get_counter(db, name)->last_updated = cur_time;
}

void free_string_key(char* strkey) {
    free(strkey);
}

void free_int_key(uint64_t* intkey) {
    free(intkey);
}

/**
 * Initialize a new NameDB struct
 * - memset it to 0
 * - set all fields to the given values
 */
void namedb_new(NameDB* db, int fd, void* region, size_t size, const char* fname) {
    db->fd = fd;
    db->region = region;
    db->max_size = size;
    db->hashes = g_hash_table_new_full(g_int64_hash, g_int64_equal, (GDestroyNotify)free_int_key, NULL);
    db->fname = fname;
}

/**
 * Load in / create a new names.db
 */
void namedb_load(NameDB* database, const char* fname, size_t size) {
    int fd;
    do {
        if(errno == EINTR)
            errno = 0;
        if((fd = open(fname, O_CREAT | O_RDWR, S_IRWXU)) == -1) {
            if(errno != EINTR) {
                perror("open: load_names");
                exit(EXIT_FAILURE);
            }
        }
    } while(errno == EINTR);

    off_t fsize = get_fsize(fd);
    bool empty = false;
    if (fsize == 0) {
        empty = true;
        if(size > INT64_MAX) {
            fprintf(stderr, "counters.db is too large, can't load\n");
            exit(EXIT_FAILURE);
        }
        truncate_file(fd, (off_t)size);
    }
    if(fsize < 0) {
        fprintf(stderr, "counters.db is negative... exiting\n");
        exit(EXIT_FAILURE);
    }
    if((size_t)fsize > size) {
        size = (size_t)fsize;
    }
    void* region;
    if((region = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
        perror("mmap: NameDB Region");
        exit(EXIT_FAILURE);
    }
    namedb_new(database, fd, region, size, fname);
    DBHeader* header = (DBHeader* )database->region;
    if (empty) {
        memset(header, 0, sizeof(DBHeader));
        header->number = 0;
        header->last_offset = sizeof(DBHeader);
    }
    size_t dbsize = header->last_offset;
    size_t name_count = namedb_length(database);
    if(name_count > 0) {
        char** names = namedb_get_names(database);
        for(uint64_t index = 0; index < name_count; index++) {
            uint64_t* hash = malloc(sizeof(uint64_t));
            *hash = g_str_hash(names[index]);
            g_hash_table_insert(database->hashes, hash, names[index]);
        }
        free(names);
    }
    database->current_size = dbsize;
}

/**
 * unmap the file used as a backing store for the NameDB
 * close that file descriptor
 * free the NameDB struct
 */
void namedb_unload(NameDB* db) {
    if (munmap(db->region, db->max_size) == -1) {
        perror("munmap");
        abort();
    }
    do {
        if(errno == EINTR)
            errno = 0;
        if(close(db->fd) == -1) {
            if(errno != EINTR) {
                perror("close: unload_names");
                exit(EXIT_FAILURE);
            }
        }
    } while(errno == EINTR);
    g_hash_table_destroy(db->hashes);

}

/**
 * Add a name to the NameDB
 *  append the size of the given name and the names.db file
 */
void namedb_add_name(NameDB* db, const char* name) {
    uint64_t name_size = sizeof(char) * (strlen(name) + 1);
    if(db->current_size + sizeof(NameHeader) + name_size >= db->max_size) {
        namedb_expand(db);
        fprintf(stderr, "expanded namedb: cur_size=%lu max_size=%lu\n", db->current_size, db->max_size);
    }
    DBHeader* header = (DBHeader* )db->region;
    NameHeader* nheader = (NameHeader* )((void* )((char* )db->region + header->last_offset + 1));
    memset(nheader, 0, sizeof(NameHeader));
    nheader->name_size = name_size;
    nheader->gc_flag = false;
    void* savedNamePtr = (void* )((char* )db->region + header->last_offset + sizeof(NameHeader) + 1);
    char* savedName = (char* )savedNamePtr;
    memset(savedName, 0, nheader->name_size);
    memmove(savedName, name, nheader->name_size);
    header->last_offset += sizeof(NameHeader) + nheader->name_size;
    header->number++;
    db->current_size += sizeof(NameHeader) + nheader->name_size;
    uint64_t* hash = malloc(sizeof(uint64_t));
    *hash = g_str_hash(name);
    g_hash_table_insert(db->hashes, hash, savedName);
}


/**
 * Return the number of names in the NameDB
 */
uint64_t namedb_length(NameDB* db) {
    DBHeader* header = (DBHeader* )db->region;
    return header->number;
}

/**
 * Return a 2D array containing references to all the names in the NameDB
 * the array returned is calloc'd remember to call free_names on it.
 */
char** namedb_get_names(NameDB* db) {
    uint64_t length = namedb_length(db);
    if (length == 0) {
        return NULL;
    }
    char** names;
    if((names = calloc(length, sizeof(char*))) == NULL) {
        perror("calloc: **names");
        exit(EXIT_FAILURE);
    }
    uint64_t offset = sizeof(DBHeader);
    for (uint64_t i = 0; i < length; ++i) {
        NameHeader* nheader = (NameHeader* )((void* )((char* )db->region + offset + 1));
        char* ondiskName = (char* )db->region + offset + sizeof(NameHeader) + 1;
        names[i] = ondiskName;
        offset = offset + sizeof(NameHeader) + nheader->name_size;
    }
    return names;
}


char* namedb_name_from_hash(NameDB* db, uint64_t hash) {
    return g_hash_table_lookup(db->hashes, &hash);
}

uint64_t counterdb_length(CounterDB* db) {
    return g_hash_table_size(db->index);
}

char** counterdb_get_names(CounterDB* db) {
    char** names;
    if((names = calloc(counterdb_length(db), sizeof(char*))) == NULL) {
        perror("calloc: counter_names");
        exit(EXIT_FAILURE);
    }
    GList* nameList = g_hash_table_get_keys(db->index);
    for(unsigned int i=0; i<g_list_length(nameList); ++i) {
        char* name = g_list_nth_data(nameList, i);
        names[i] = name;
    }
    g_list_free(nameList);
    return names;
}

Counter** counterdb_get_counters(CounterDB* db) {
    Counter** counters;
    if((counters = calloc(counterdb_length(db), sizeof(Counter*))) == NULL) {
        perror("malloc: get_counters");
        exit(EXIT_FAILURE);
    }
    GList* counterList = g_hash_table_get_values(db->index);
    for(unsigned int i=0; i<g_list_length(counterList); ++i) {
        counters[i] = g_list_nth_data(counterList, i);
    }
    g_list_free(counterList);
    return counters;
}

/**
 * Scan the database and fill the freespace cache
 * called on startup, and when the cache is empty and we know
 * there are things to add to it
 */
void counterdb_fill_fsc(CounterDB* db) {
    uint64_t num_counters = counterdb_length(db);
    Counter** counters = counterdb_get_counters(db);
    for(uint64_t index = 0; index < num_counters; index++) {
        if(counters[index]->gc_flag) {
            g_queue_push_head(db->freespace_cache, counters[index]);
        }
    }
    free(counters);
    fprintf(stderr, "Added %u entries to the FSC\n", g_queue_get_length(db->freespace_cache));
}

/**
 * Mark a sigle name in the NameDB as needing to be GC'd
 * This name will be expunged during the next compaction run
 */
void namedb_gc_mark_name(NameDB* db, uint64_t name_hash) {
    char* name = namedb_name_from_hash(db, name_hash);
    NameHeader* nheader = (NameHeader*)((void*)(name - sizeof(NameHeader)));
    nheader->gc_flag = true;
}

/**
 * Mark all counters that should be deleted as such
 * These marked counters will eventually be put into the freespace_cache and
 * their memory reused.
 */
void counterdb_gc_mark(CounterDB* db) {
    uint64_t num_counters = counterdb_length(db);
    Counter** counters = counterdb_get_counters(db);
    uint64_t num_marked = 0;
    for(uint64_t index = 0; index < num_counters; index++) {
        //120 == mark counters that have gone 2 minutes without change
        if(time(NULL) - counters[index]->last_updated >= 60 && !counters[index]->gc_flag) {
            counters[index]->gc_flag = true;
            namedb_gc_mark_name(db->names, counters[index]->name_hash);
            db->gc_changed = true;
            num_marked++;
        }
    }
    fprintf(stderr, "Marked %lu counters for GC\n", num_marked);
    free(counters);
}

void namedb_compact(NameDB* db) {
    NameDB* compacted;
    if((compacted = malloc(sizeof(NameDB))) == NULL) {
        perror("malloc: namedb_compact");
        exit(EXIT_FAILURE);
    }
    namedb_load(compacted, "names.db.compacted", db->max_size);
    const char* fname = db->fname;
    uint64_t size = db->max_size;
    char** names = namedb_get_names(db);
    uint64_t length = namedb_length(db);
    for(uint64_t i = 0; i<length; i++) {
        NameHeader* nheader = (NameHeader*)((void*)(names[i] - sizeof(NameHeader)));
        if(!nheader->gc_flag) {
            namedb_add_name(compacted, names[i]);
        }
    }
    namedb_unload(compacted);
    namedb_unload(db);
    if(rename("names.db.compacted", fname) == -1) {
        perror("rename: namedb_compact");
        exit(EXIT_FAILURE);
    }
    namedb_load(db, fname, size);
    fprintf(stderr, "Compacted namesdb: %lu -> %lu\n", length, namedb_length(db));
}
