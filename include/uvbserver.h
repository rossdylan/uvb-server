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
    struct evhttp_bound_socket* handle;
    char* addr;
    CounterDB* database;
    uint16_t port;
} UVBServer;

void uvbserver_new(UVBServer* serv, struct event_base* base, char* addr, uint16_t port, CounterDB* database);
void uvbserver_free(UVBServer* serv);

/**
 * the void* arg argument contains the CounterDB
 * Dispatch http requests to the proper handlers.
 * used for:
 * /register/<name> - Register a new user
 * /<name> - increment counter
 */
void uvbserver_route_dispatch(struct evhttp_request* req, void* arg);

/**
 * display the score counters
 * the void* arg argument contains the CounterDB
 */
void uvbserver_route_display(struct evhttp_request* req, void* arg);

/**
 * Return the number of tokens that will be created using ntok
 * on the given string with the given deliminator. If someone manages to overflow
 * a uint64_t it will return 0;
 */
uint64_t ntok(char* str, const char* delim);

/**
 * Create an array of strings representing all the tokens made from the string
 */
char** split(char* str, const char* delim, uint64_t n);

/**
 * Clean up the array created by split
 */
void free_split(char** s, uint64_t size);

void uvbserver_calculate_rps(int fd, short event, void *arg);
#endif
