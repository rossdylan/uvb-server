#include "uvbstore.h"
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

int main(int argc, char** argv) {
	CounterDB* database = load_database(10 * sysconf(_SC_PAGE_SIZE));
	if(!counter_exists(database, "rossdylan")) {
		add_counter(database, "rossdylan");
	}
	if(!counter_exists(database, "tonystark")) {
		add_counter(database, "tonystark");
	}
	increment_counter(database, "rossdylan");
	increment_counter(database, "tonystark");
	increment_counter(database, "tonystark");
	unload_database(database);

	// now load it back in
	database = load_database(10 * sysconf(_SC_PAGE_SIZE));
	Counter* rdc = get_counter(database, "rossdylan");
	Counter* tsc = get_counter(database, "tonystark");
	printf("The %s counter: %lu\n", "rossdylan", rdc->count);
	printf("The %s counter: %lu\n", "tonystark", tsc->count);
	unload_database(database);
}
