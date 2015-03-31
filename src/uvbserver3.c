#include "server.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <errno.h>


/**
 * Utility Functions
 */

const char *make_http_response(int status_code, const char *status, const char *content_type, const char* response) {
    char *full_response;
    asprintf(&full_response, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n\r\n%s",
            status_code, status, content_type, strlen(response), response);
    return full_response;
}

// Set a socket fd to be nonblocking
int unblock_socket(int fd) {
    int flags;
    if((flags = fcntl(fd, F_GETFL, 0)) == -1) {
        perror("fcntl");
        return -1;
    }
    flags |= O_NONBLOCK;
    if(fcntl(fd, F_SETFL, flags) == -1) {
        perror("fcntl");
        return -1;
    }
    return 0;
}


// Return a server fd
int make_server_socket(const char *port) {
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int s = 0, server_fd = 0;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if(getaddrinfo(NULL, port, &hints, &result) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        return -1;
    }
    for(rp = result; rp != NULL; rp = rp->ai_next) {
        server_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if(server_fd == -1) {
            continue;
        }
        if(bind(server_fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(server_fd);
    }
    if(rp == NULL) {
        fprintf(stderr, "Could not bind\n");
        return -1;
    }
    freeaddrinfo(result);
    int reuse = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    return server_fd;
}


static inline bool epoll_error(struct epoll_event e) {
    return e.events & EPOLLERR || e.events & EPOLLHUP || !(e.events & EPOLLIN);
}


void init_connection(connection_t *session, int fd) {
    session->fd = fd;
    http_parser_init(&session->parser, HTTP_REQUEST);
    init_http_msg(&session->msg);
    session->done = false;
}

void free_connection(connection_t *session) {
    close(session->fd);
    free_http_msg(&session->msg);
    // I May need to do some tear down of the parser, idk
    free(session);
}

// Function executed within a pthread to multiplex epoll acrossed threads
void *epoll_loop(void *ptr) {
    //set up our parser settings.
    http_parser_settings parser_settings;
    parser_settings.on_url = on_url;
    parser_settings.on_header_field = on_header_field;
    parser_settings.on_header_value = on_header_value;
    parser_settings.on_headers_complete = on_headers_complete;
    parser_settings.on_message_begin = NULL;
    parser_settings.on_status_complete = NULL;
    parser_settings.on_message_complete = NULL;
    parser_settings.on_body = NULL;

    thread_data_t *data = ptr;
    const char *response = make_http_response(200, "OK", "text/plain", "YOLO");
    struct epoll_event *events;
    struct epoll_event event;
    if((events = calloc(MAXEVENTS, sizeof(struct epoll_event))) == NULL) {
        perror("calloc");
        return NULL;
    }
    int waiting;
    connection_t *session;
    while(true) {
        waiting = epoll_wait(data->epoll_fd, events, MAXEVENTS, -1);
        if(waiting < 0) {
            if(errno != EINTR) {
                perror("epoll_wait");
                return NULL;
            }
        }
        for(int i=0; i<waiting; i++) {
            session = (connection_t *)events[i].data.ptr;
            set_current_session(session);
            if(session == NULL) {
                continue;
            }
            if(epoll_error(events[i])) {
                free_connection(session);
                events[i].data.ptr = NULL;
                printf("EPOLL ERROR, cleaning up\n");
                continue;
            }
            else if(data->listen_fd == session->fd) {
                struct sockaddr in_addr;
                socklen_t in_len = sizeof(in_addr);
                int in_fd = -1;
                if((in_fd = accept(data->listen_fd, &in_addr, &in_len)) == -1) {
                    if((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                        goto epoll_loop_server_reenable;
                    }
                    else {
                        perror("accept");
                        goto epoll_loop_server_reenable;
                    }
                }
                if(unblock_socket(in_fd) == -1) {
                    //TODO maybe do extra handling of this error case?
                    perror("unblock_socket");
                    goto epoll_loop_server_reenable;
                }
                if((event.data.ptr = malloc(sizeof(connection_t))) == NULL) {
                    perror("malloc");
                    goto epoll_loop_server_reenable;
                }
                connection_t *new_session = (connection_t *)event.data.ptr;
                init_connection(new_session, in_fd);
                event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                if(epoll_ctl(data->epoll_fd, EPOLL_CTL_ADD, in_fd, &event) != 0) {
                    perror("epoll_ctl");
                    free_connection(new_session);
                    goto epoll_loop_server_reenable;
                }
                // Renable our server fd in epoll
epoll_loop_server_reenable:
                events[i].events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                if(epoll_ctl(data->epoll_fd, EPOLL_CTL_MOD, session->fd, &events[i]) != 0) {
                    perror("epoll_ctl");
                    continue;
                }
            }
            else {
                while(1) {
                    char buf[512];
                    ssize_t count = -1;
                    if((count = read(session->fd, buf, sizeof(buf))) == -1) {
                        if(errno != EAGAIN) {
                            perror("read");
                            session->done = true;
                        }
                        break;
                    }
                    else if(count == 0) {
                        session->done = true;
                        break;
                    }

                    // Since we check if count is -1 and back out before this point this cast should be safe
                    size_t parsed = http_parser_execute(&session->parser, &parser_settings, buf, (size_t)count);
                    if(parsed != (size_t)count) {
                        // ERROR OH NO
                        session->done = true;
                        break;
                    }
                    // REMOVE BUFFER SHIT - ADD PARSER SHIT
                    if(session->msg.done) {
                        if(http_url_compare(&session->msg, "/stats") == 0) {
                            uint64_t value = global_counter_value(data->counter);
                            char *thingy;
                            asprintf(&thingy, "VALUE = %lu\n", value);
                            char *resp = make_http_response(200, "OK", "text/plain", thingy);
                            if(write(session->fd, resp, strlen(resp)) == -1) {
                                perror("write");
                            }
                            free(resp);
                            free(thingy);
                        }
                        else {
                            counter_inc(data->counter, data->thread_id);
                            if(write(session->fd, response, strlen(response)) == -1) {
                                perror("write");
                            }
                        }
                        session->done = true;
                        break;
                    }
                }
                if(session->done) {
                    free_connection(session);
                    events[i].data.ptr = NULL;
                }
                else {
                    events[i].events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                    if(epoll_ctl(data->epoll_fd, EPOLL_CTL_MOD, session->fd, &events[i]) != 0) {
                        perror("epoll_ctl");
                    }
                }
            }

        }
    }
}

server_t *new_server(const size_t nthreads, const char *addr, const char *port) {
    // set up out thread local storage for the current session
    pthread_key_create(&current_session, NULL);
    server_t *server = NULL;
    global_counter_t *global_counter = NULL;
    if((server = malloc(sizeof(server_t))) == NULL) {
        perror("malloc");
        return NULL;
    }
    if((global_counter = new_global_counter(nthreads)) == NULL) {
        goto new_server_free;
    }
    server->nthreads = nthreads;
    server->address = addr;
    server->port = port;
    struct epoll_event event;
    // Make our array of threads
    if((server->threads = calloc(nthreads, sizeof(pthread_t))) == NULL) {
        perror("calloc");
        server->threads = NULL;
        goto new_server_free;
    }
    // make our epoll file descriptor
    if((server->epoll_fd = epoll_create1(0)) == -1) {
        perror("epoll_create");
        goto new_server_free;
        return NULL;
    }
    // setup our server socket
    if((server->listen_fd = make_server_socket(server->port)) == -1) {
        perror("make_server_socket");
        goto new_server_listen_close;
    }
    // set up the server sockets epoll event data
    if((event.data.ptr = malloc(sizeof(connection_t))) == NULL) {
        perror("malloc");
        goto new_server_epoll_close;
    }
    connection_t *new_session = (connection_t *)event.data.ptr;
    new_session->fd = server->listen_fd;
    new_session->done = false;
    if(unblock_socket(server->listen_fd) == -1) {
        perror("unblock_socket");
        goto new_server_epoll_close;
    }
    if(listen(server->listen_fd, SOMAXCONN) == -1) {
        perror("listen");
        goto new_server_epoll_close;
    }
    event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
    if(epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, server->listen_fd, &event) == -1) {
        perror("epoll_create");
        goto new_server_epoll_close;
    }


    for(size_t i=0; i<nthreads; i++) {
        thread_data_t *tdata = NULL;
        if((tdata = malloc(sizeof(thread_data_t))) == NULL) {
            perror("malloc");
            goto new_server_epoll_close;
        }
        tdata->epoll_fd = server->epoll_fd;
        tdata->listen_fd = server->listen_fd;
        tdata->thread_id = i;
        tdata->counter = global_counter; // this bit is shared data
        if(pthread_create(&server->threads[i], NULL, epoll_loop, (void *)tdata) != 0) {
            perror("pthread_create");
            goto new_server_epoll_close;
        }
    }
    goto new_server_return;

new_server_epoll_close:
    close(server->epoll_fd);
new_server_listen_close:
    close(server->listen_fd);
new_server_free:
    free(server->threads);
    free(server);
    free(event.data.ptr);
    server = NULL;
    if(global_counter != NULL) {
        global_counter_free(global_counter);
    }
new_server_return:
    return server;
}

void server_wait(server_t *server) {
    for(size_t i=0; i<server->nthreads; i++) {
        pthread_join(server->threads[i], NULL);
    }
}

int main(int argc, char *argv[]) {
    server_t *server = new_server(8, "0.0.0.0", "8000");
    server_wait(server);
}
