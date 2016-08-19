/**
 * File: server.h
 * Headers for the core UVB server code. Handles main func, sockets, and event
 * loops.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>
#include "buffer.h"
#include "counter.h"
#include "http.h"
#include "timers.h"


#define MAXEVENTS 64
#define MAXREAD 512
#define STATS_SECS 10

/**
 * Structure for the actual server. Stores the pthread handles the number of
 * threads, and the port
 */
typedef struct {
    const char *port;
    size_t nthreads;
    timer_mgr_t timers;
    pthread_t *threads;
} server_t;


/**
 * Structure passed into each thread in order to give them the info they need
 * to set themselves up. Contains the threads ID, and places to store the 
 * epoll fd server fd, and a reference to the counter implementation.
 */
typedef struct {
    const char *port;
    int listen_fd;
    int epoll_fd;
    void *data;
    uint64_t thread_id;
} thread_data_t;


int make_http_response(char **resp_ptr, int status_code, const char *status, const char *content_type, const char* response);
int unblock_socket(int fd);
int make_server_socket(const char *port);
void free_connection(connection_t *session);
void init_connection(connection_t *session, int fd);
void *epoll_loop(void *ptr);
server_t *new_server(const size_t nthreads, const char *addr, const char *port);
void server_wait(server_t *server);
