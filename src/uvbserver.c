#include "uvbserver.h"
#include <stdlib.h>


/**
 * Take a freshly allocated UVBServer struct and fill it up and set all the values to their defaults
 * This includes:
 *  making the evhttp struct
 *  setting the default route handler to uvb_unknown_route
 *  tell evhttp where to listen
 */
void new_uvbserver(UVBServer* serv, struct event_base* base, char* addr, int port) {
  serv->http = evhttp_new(base);
  evhttp_set_gencb(serv->http, uvb_unknown_route, NULL);
  serv->handle = evhttp_bind_socket_with_handle(serv->http, addr, port);
  if(!serv->handle) {
    fprintf(stderr, "couldn't bind to %s:%d", addr, port);
    exit(EXIT_FAILURE);
  }
}


/**
 * Free all the evhttp structs contained within UVBServer and then free the UVBServer struct
 */
void free_uvbserver(UVBServer* serv) {
  free(serv->handle);
  serv->handle = NULL;
  free(serv->http);
  serv->http = NULL;
  free(serv->addr);
  serv->addr = NULL;
  free(serv);
  serv = NULL;
}

void new_uvbroute(UVBRoute* route, char* path, void* handler) {
  route->path = path;
  route->handler = handler;
}

void free_uvbroute(UVBRoute* route) {
  free(route);
}

void add_uvbroute(UVBServer* serv, UVBRoute* route) {
  evhttp_set_cb(serv->http, route->path, route->handler, NULL);
}

void uvb_unknown_route(struct evhttp_request* req, void* arg) {
  evhttp_send_reply(req, 404, "Not Found", NULL);
}
