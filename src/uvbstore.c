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

/**
 * Initialize a new CounterDB by memsetting it to 0 and setting all its values
 */
void new_counterdb(CounterDB* db, int fd, uint8_t* region, uint64_t size, uint64_t cur_size) {
  db->fd = fd;
  db->region = region;
  db->max_size = size;
  db->current_size = cur_size;
  db->index = g_hash_table_new(g_str_hash, g_str_equal);
  db->names = load_names(size);
}

/**
 * Load in a database of the given size
 * @TODO(rossdylan) verify that the size given is the correct size, if not,
 * remap with more space
 */
CounterDB* load_database(uint64_t size) {
  int fd;
	while(true) {
		if ((fd = open("./counters.db", O_CREAT | O_RDWR, S_IRWXU)) == -1) {
			if(errno == EINTR) {
				continue;
			}
			perror("Failed open() to load page");
			exit(1);
		}
		break;
  }
  struct stat* the_stats;
  if((the_stats = malloc(sizeof(struct stat))) == NULL) {
    perror("malloc: load_database: stat:");
    exit(EXIT_FAILURE);
  }
  fstat(fd, the_stats);
  bool empty = false;
  if (the_stats->st_size == 0) {
    empty = true;
    if(ftruncate(fd, size) == -1) {
			perror("ftruncate: load_database");
			exit(EXIT_FAILURE);
		}
  }
  free(the_stats);
  the_stats = NULL;
  CounterDB* database;
  if((database = calloc(1, sizeof(CounterDB))) == NULL) {
    perror("calloc: CounterDB");
    exit(EXIT_FAILURE);
  }
  uint8_t* region;
  if((region = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
    perror("mmap: CounterDB Region");
    exit(EXIT_FAILURE);
  }
  DBHeader* ncounters = (DBHeader* )region;
  if (empty) {
    memset(ncounters, 0, sizeof(DBHeader));
    ncounters->number = 0;
    ncounters->last_offset = sizeof(DBHeader);
  }
  uint64_t current_size = sizeof(DBHeader) + sizeof(Counter) * ncounters->number;
  new_counterdb(database, fd, region, size, current_size);
  if (!empty) {
    load_index(database);
  }
  fprintf(stderr, "Loaded %lu counters\n", num_counters(database));
  return database;
}

/**
 * Scan the names.db and counters.db to rebuild the in memory
 * index (GHashTable)
 */
void load_index(CounterDB* db) {
  DBHeader* num_counters = (DBHeader* )db->region;
  Counter* current;
  int name_length = names_length(db->names);
  if (name_length > 0) {
    char** names = get_names(db->names);
    for (int i = 0; i < name_length; ++i) {
      g_quark_from_string(names[i]);
    }
    free_names(names, name_length);
  }
  for (int index = 0; index < num_counters->number; ++index) {
    current = (Counter* )db->region + sizeof(DBHeader) + (index * sizeof(Counter)) + 1;
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
  int rval;
  do {
    rval = close(db->fd);
  }
  while(errno == EINTR);
  if(rval == -1) {
    perror("close");
    abort();
  }
  unload_names(db->names);
  g_hash_table_destroy(db->index);
  free(db);
  db = NULL;
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
  DBHeader* ncounters = (DBHeader* )db->region;
  Counter* new_counter = (Counter* )db->region + ((ncounters->number * sizeof(Counter)) + sizeof(DBHeader) + 1);
  memset(new_counter, 0, sizeof(Counter));
  new_counter->count = 0;
  new_counter->name_quark = g_quark_from_string(name);
  ncounters->number++;
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
  return g_hash_table_contains(db->index, name);
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
void new_namedb(NameDB* db, int fd, uint8_t* region, uint64_t size) {
  memset(db, 0, sizeof(NameDB));
  db->fd = fd;
  db->region = region;
  db->size = size;
}

/**
 * Load in / create a new names.db
 */
NameDB* load_names(uint64_t size) {
  int fd;
	while(true) {
		if ((fd = open("./names.db", O_CREAT | O_RDWR, S_IRWXU)) == -1) {
			if(errno == EINTR) {
				continue;
			}
			perror("Failed open() to load page");
			exit(1);
		}
		break;
  }
  struct stat* the_stats;
  if((the_stats = malloc(sizeof(struct stat))) == NULL) {
    perror("malloc: load_names: stat:");
    exit(EXIT_FAILURE);
  }
  fstat(fd, the_stats);
  bool empty = false;
  if (the_stats->st_size == 0) {
    empty = true;
    if(ftruncate(fd, size)) {
			perror("ftruncate: load_names");
			exit(EXIT_FAILURE);
		}
  }
  free(the_stats);
  the_stats = NULL;
  NameDB* database;
  if((database = calloc(1, sizeof(NameDB))) == NULL) {
    perror("calloc: NameDB");
    exit(EXIT_FAILURE);
  }
  uint8_t* region;
	if((region = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == NULL) {
    perror("mmap: NameDB Region");
    exit(EXIT_FAILURE);
	}
  new_namedb(database, fd, region, size);
  if (empty) {
    DBHeader* header = (DBHeader* )database->region;
    memset(header, 0, sizeof(DBHeader));
    header->number = 0;
    header->last_offset = sizeof(DBHeader);
  }
  return database;
}

/**
 * unmap the file used as a backing store for the NameDB
 * close that file descriptor
 * free the NameDB struct
 */
void unload_names(NameDB* db) {
  if (munmap(db->region, db->size) == -1) {
    perror("munmap");
    abort();
  }
	int rval;
	do {
		rval = close(db->fd);
	}
	while(errno == EINTR);
	if(rval == -1) {
    perror("close");
    abort();
  }
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
  uint64_t* savedNameSize = (uint64_t* )db->region + header->last_offset + 1;
  memset(savedNameSize, 0, sizeof(uint64_t));
  *savedNameSize = nameSize;
  char* savedName = (char* )db->region + header->last_offset + sizeof(uint64_t) + 1;
  memset(savedName, 0, nameSize);
  memmove(savedName, name, nameSize);
  header->last_offset += sizeof(uint64_t) + nameSize;
  header->number++;
}

/**
 * Return the number of names in the NameDB
 */
int names_length(NameDB* db) {
  DBHeader* header = (DBHeader* )db->region;
  return header->number;
}

/**
 * Return a 2D array containing references to all the names in the NameDB
 * the array returned is calloc'd remember to call free_names on it.
 */
char** get_names(NameDB* db) {
  int length = names_length(db);
  if (length == 0) {
    return NULL;
  }
  char** names;
  if((names = calloc(length, sizeof(char*))) == NULL) {
    perror("calloc: **names");
    exit(EXIT_FAILURE);
  }
  uint64_t offset = sizeof(DBHeader);
  for (int i = 0; i < length; ++i) {
    uint64_t size = *((uint64_t* )db->region + offset + 1);
    if((names[i] = calloc(1, size)) == NULL) {
      perror("calloc: *name");
      exit(EXIT_FAILURE);
    }
		memmove(names[i], (void* )db->region + offset + sizeof(uint64_t) + 1, size);
    offset += sizeof(uint64_t) + size;
  }
  return names;
}

/**
 * Go through the names 2D array and free everything
 */
void free_names(char** names, int len) {
  for (int i = 0; i < len; ++i) {
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
  for(int i=0; i<g_list_length(nameList); ++i) {
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
