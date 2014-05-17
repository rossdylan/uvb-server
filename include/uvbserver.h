#ifndef _UVB_UVBSERVER_H_
#define _UVB_UVBSERVER_H_

#include <event2/event.h>
#include <event2/http.h>
#include "uvbstore.h"

typedef struct {
  void* handler;
  char* path;
} UVBRoute;

typedef struct {
  struct evhttp* http;
  struct evhttp_bound_socket *handle;
  char* addr;
  int port;
  CounterDB* database;
} UVBServer;

void new_uvbserver(UVBServer* serv, struct event_base* base, char* addr, int port, CounterDB* database);
void free_uvbserver(UVBServer* serv);

/**
 * the void* arg argument contains the CounterDB
 * Dispatch http requests to the proper handlers.
 * used for:
 * /register/<name> - Register a new user
 * /<name> - increment counter
 */
void uvb_route_dispatch(struct evhttp_request* req, void* arg);

/**
 * display the score counters
 * the void* arg argument contains the CounterDB
 */
void uvb_route_display(struct evhttp_request* req, void* arg);
#endif
