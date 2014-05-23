#include "uvbstore.h"
#include "uvbserver.h"
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <event2/event.h>
#include <event2/http.h>
#include <signal.h>


static void usage(char* name) {
    fprintf(stderr, "Usage: %s <address> <port>\n", name);
}

int main(int argc, char** argv) {
    if(argc < 3) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    char* strol_end;
    int64_t inputPort = strtol(argv[2], &strol_end, 10);
    if(*strol_end) {
        fprintf(stderr, "<port> must be a valid integer\n");
        exit(EXIT_FAILURE);
    }
    if(inputPort > UINT16_MAX || inputPort <= 0) {
        fprintf(stderr, "Please enter a valid port number\n");
        exit(EXIT_FAILURE);
    }
    uint16_t validPort = (uint16_t)inputPort;
    int64_t tenpages = 10 * sysconf(_SC_PAGE_SIZE);
    if(tenpages < 0) {
        fprintf(stderr, "Your page size is < 0, wtf\n");
        exit(EXIT_FAILURE);
    }
    size_t dbsize = (size_t)tenpages;

    CounterDB* database;
    if((database = calloc(1, sizeof(CounterDB))) == NULL) {
        perror("calloc: main: database");
        exit(EXIT_FAILURE);
    }
    counterdb_load(database, dbsize);
    UVBServer* server;
    if((server = calloc(1, sizeof(UVBServer))) == NULL) {
        perror("calloc: UVBServer");
        exit(EXIT_FAILURE);
    }
    struct event_base* base = event_base_new();
    uvbserver_new(server, base, argv[1], validPort, database);
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    if (sigemptyset(&sa.sa_mask) == -1 || sigaction(SIGPIPE, &sa, 0) == -1) {
        perror("failed to ignore SIGPIPE; sigaction");
        exit(EXIT_FAILURE);
    }
    event_base_dispatch(base);
    uvbserver_free(server);
    return 0;
}
