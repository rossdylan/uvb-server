#include "uvbstore.h"
#include "uvbserver.h"
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <event2/event.h>
#include <event2/http.h>


static void usage(char* name) {
  fprintf(stderr, "Usage: %s <address> <port>\n", name);
}

int main(int argc, char** argv) {
  if(argc < 3) {
    usage(argv[0]);
    exit(EXIT_FAILURE);
  }

  char* strol_end;
  int port = strtol(argv[2], &strol_end, 10);
  if(*strol_end) {
    fprintf(stderr, "<port> must be a valid integer\n");
    exit(EXIT_FAILURE);
  }
  CounterDB* database = load_database(10 * sysconf(_SC_PAGE_SIZE));
  UVBServer* server;
  if((server = calloc(1, sizeof(UVBServer))) == NULL) {
    perror("calloc: UVBServer");
    exit(EXIT_FAILURE);
  }
  struct event_base* base = event_base_new();
  new_uvbserver(server, base, argv[1], port, database);
  event_base_dispatch(base);
  free_uvbserver(server);
  exit(EXIT_SUCCESS);
}
