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
void new_counterdb(CounterDB* db, int fd, void* region, size_t size, size_t cur_size) {
    db->fd = fd;
    db->region = region;
    db->max_size = size;
    db->current_size = cur_size;
    db->index = g_hash_table_new(g_str_hash, g_str_equal);
    db->names = load_names(size);
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

void expand_database(CounterDB* db) {
    off_t incr = 10 * sysconf(_SC_PAGE_SIZE);
    // first we expand the counters file
    off_t cfsize = get_fsize(db->fd);
    off_t newcfsize = cfsize + incr;
    truncate_file(db->fd, newcfsize);
    // now we expand the names file
    //
    off_t nfsize = get_fsize(db->names->fd);
    off_t newnfsize = nfsize + incr;
    truncate_file(db->names->fd, newnfsize);
    // now we unmap and remap
    unload_database(db);
    if(newcfsize < 0) {
        fprintf(stderr, "The database has been made into a negative size\n");
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "Database is now %ld\n", newcfsize);
    db->names = load_names((uint64_t)newnfsize);
    load_database(db, (uint64_t)newcfsize);
    fprintf(stderr, "Expanded Database to %ld\n", newcfsize);
}


/**
 * Load in a database of the given size
 * TODO(rossdylan) verify that the size given is the correct size, if not,
 * remap with more space
 */
void load_database(CounterDB* database, size_t size) {
    int fd;
    do {
        if(errno == EINTR)
            errno = 0;
        if((fd = open("./counters.db", O_CREAT | O_RDWR, S_IRWXU)) == -1) {
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
    new_counterdb(database, fd, region, size, current_size);
    if (!empty) {
        load_index(database);
    }
    fprintf(stderr, "Loaded %lu counters\n", num_counters(database));
}

/**
 * Scan the names.db and counters.db to rebuild the in memory
 * index (GHashTable)
 */
void load_index(CounterDB* db) {
    Counter* current;
    DBHeader* header = (DBHeader* )db->region;
    uint64_t name_length = names_length(db->names);
    if (name_length > 0) {
        char** names = get_names(db->names);
        for (uint64_t i = 0; i < name_length; ++i) {
            g_quark_from_string(names[i]);
        }
        free_names(names, name_length);
    }
    for (uint64_t index = 0; index < header->number; ++index) {
        current = (Counter* )(db->region + sizeof(DBHeader) + (index * sizeof(Counter)) + 1);
        const char* name = g_quark_to_string(current->name_quark);
        g_hash_table_insert(db->index, (gpointer)name, current);
    }
}

/**
 * Destroy a CounterDB struct:
 * - unmap the memory region
 * - close the file descriptor
 * - destroy the GHashTable
 */
void unload_database(CounterDB* db) {
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
    unload_names(db->names);
    g_hash_table_destroy(db->index);
}

/**
 * Add a new counter to the database:
 * - write new counter to counters.db
 * - update counters.db metadata (offset/num counters)
 * - add name to the names.db
 * - add name -> Counter* mapping to the index
 *   XXX(rossdylan) Add a check to make sure we don't overrun the end of our DB
 */
Counter* add_counter(CounterDB* db, const char* name) {
    DBHeader* header = (DBHeader* )db->region;
    if((db->current_size + sizeof(Counter)) >= db->max_size || (db->names->current_size + sizeof(uint64_t) + (sizeof(char) * (strlen(name) + 1))) >= db->names->max_size) {
        expand_database(db);
        fprintf(stderr, "expanded: cur_size=%lu max_size=%lu\n", db->current_size, db->max_size);
    }
    header = (DBHeader* )db->region;
    Counter* new_counter = (Counter* )(db->region + header->last_offset + 1);
    memset(new_counter, 0, sizeof(Counter));
    new_counter->count = 0;
    new_counter->name_quark = g_quark_from_string(name);
    header->number++;
    header->last_offset = header->last_offset + sizeof(Counter);
    size_t nlen = (strlen(name) + 1);
    char* localname = calloc(nlen, sizeof(char));
    memmove(localname, name, sizeof(char) * nlen);
    g_hash_table_insert(db->index, (gpointer)localname, new_counter);
    db->current_size += sizeof(Counter);
    add_name(db->names, name);
    return new_counter;
}

/**
 * check if a counter exists.
 * This doesn't go to disk through the mmap'd file, instead it just
 * checks the index. This is fine because during normal run time everything on
 * disk is fully mirrored into index.
 */
bool counter_exists(CounterDB* db, const char* name) {
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
Counter* get_counter(CounterDB* db, const char* name) {
    Counter* counter = g_hash_table_lookup(db->index, (gpointer)name);
    return counter;
}

/**
 * Increment the given counter by 1
 */
void increment_counter(CounterDB* db, const char* name) {
    get_counter(db, name)->count++;
}

/**
 * Initialize a new NameDB struct
 * - memset it to 0
 * - set all fields to the given values
 */
void new_namedb(NameDB* db, int fd, void* region, size_t size) {
    db->fd = fd;
    db->region = region;
    db->max_size = size;
}

/**
 * Load in / create a new names.db
 */
NameDB* load_names(size_t size) {
    int fd;
    do {
        if(errno == EINTR)
            errno = 0;
        if((fd = open("./names.db", O_CREAT | O_RDWR, S_IRWXU)) == -1) {
            if(errno != EINTR) {
                perror("open: load_names");
                exit(EXIT_FAILURE);
            }
        }
    } while(errno == EINTR);

    struct stat the_stats;
    fstat(fd, &the_stats);
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
    NameDB* database;
    if((database = calloc(1, sizeof(NameDB))) == NULL) {
        perror("calloc: NameDB");
        exit(EXIT_FAILURE);
    }
    void* region;
    if((region = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
        perror("mmap: NameDB Region");
        exit(EXIT_FAILURE);
    }
    new_namedb(database, fd, region, size);
    DBHeader* header = (DBHeader* )database->region;
    if (empty) {
        memset(header, 0, sizeof(DBHeader));
        header->number = 0;
        header->last_offset = sizeof(DBHeader);
    }
    off_t dbsize = ((char* )database->region + header->last_offset) - (char* )database->region;
    if(dbsize < 0) {
        fprintf(stderr, "calculated the DBsize to be 0, exiting");
        exit(EXIT_FAILURE);
    }
    database->current_size = (uint64_t)dbsize;
    fprintf(stderr, "names.db is currently: %lu\n", database->current_size);
    return database;
}

/**
 * unmap the file used as a backing store for the NameDB
 * close that file descriptor
 * free the NameDB struct
 */
void unload_names(NameDB* db) {
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
    free(db);
    db = NULL;

}

/**
 * Add a name to the NameDB
 *  append the size of the given name and the names.db file
 */
void add_name(NameDB* db, const char* name) {
    //XXX(rossdylan) make sure we don't add a name that goes over the end of our db
    DBHeader* header = (DBHeader* )db->region;
    uint64_t nameSize = (strlen(name) + 1) * sizeof(char);
    uint64_t* savedNameSize = (uint64_t* )(db->region + header->last_offset + 1);
    memset(savedNameSize, 0, sizeof(uint64_t));
    *savedNameSize = nameSize;
    char* savedName = (char* )(db->region + header->last_offset + sizeof(uint64_t) + 1);
    memset(savedName, 0, nameSize);
    memmove(savedName, name, nameSize);
    header->last_offset += sizeof(uint64_t) + nameSize;
    header->number++;
    db->current_size += sizeof(uint64_t) + nameSize;
}

/**
 * Return the number of names in the NameDB
 */
uint64_t names_length(NameDB* db) {
    DBHeader* header = (DBHeader* )db->region;
    return header->number;
}

/**
 * Return a 2D array containing references to all the names in the NameDB
 * the array returned is calloc'd remember to call free_names on it.
 */
char** get_names(NameDB* db) {
    uint64_t length = names_length(db);
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
        uint64_t size = *(uint64_t* )(db->region + offset + 1);
        if((names[i] = calloc(1, size)) == NULL) {
            perror("calloc: *name");
            exit(EXIT_FAILURE);
        }
        char* ondiskName = (char* )db->region + offset + sizeof(uint64_t) + 1;
        memmove(names[i], (void* )ondiskName, size);
        offset += sizeof(uint64_t) + size;
    }
    return names;
}

/**
 * Go through the names 2D array and free everything
 */
void free_names(char** names, uint64_t len) {
    for (uint64_t i = 0; i < len; ++i) {
        free(names[i]);
        names[i] = NULL;
    }
    free(names);
    names = NULL;
}

uint64_t num_counters(CounterDB* db) {
    return g_hash_table_size(db->index);
}

char** counter_names(CounterDB* db) {
    char** names;
    if((names = calloc(num_counters(db), sizeof(char*))) == NULL) {
        perror("calloc: counter_names");
        exit(EXIT_FAILURE);
    }
    GList* nameList = g_hash_table_get_keys(db->index);
    for(unsigned int i=0; i<g_list_length(nameList); ++i) {
        char* name = g_list_nth_data(nameList, i);
        size_t nsize = (strlen(name) + 1) * sizeof(char);
        if((names[i] = calloc(nsize, sizeof(char))) == NULL) {
            perror("calloc: counter_names: name");
            exit(EXIT_FAILURE);
        }
        memmove(names[i], name, nsize);
    }
    g_list_free(nameList);
    return names;
}
