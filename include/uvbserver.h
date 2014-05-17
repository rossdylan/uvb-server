#ifndef _UVB_UVBSERVER_H_
#define _UVB_UVBSERVER_H_

#include "event2/event.h"
#include "event2/http.h"

typedef struct {
  void* handler;
  char* path;
} UVBRoute;

typedef struct {
  struct evhttp* http;
  struct evhttp_bound_socket *handle;
  char* addr;
  int port;
} UVBServer;

void new_uvbserver(UVBServer* serv, struct event_base* base, char* addr, int port);
void free_uvbserver(UVBServer* serv);

void new_uvbroute(UVBRoute* route, char* path, void* handler);
void free_uvbroute(UVBRoute* route);

void add_uvbroute(UVBServer* serv, UVBRoute* route);

void uvb_unknown_route(struct evhttp_request* req, void *arg);
#endif
