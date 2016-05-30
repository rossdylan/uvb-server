#define _GNU_SOURCE

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
#include <sched.h>


static const char header_page[] = "--- Ultimate Victory Battle (v4.0.0) ---\n"
                                  " Rules: \n"
                                  "  - Increment your counter higher/faster than everyone else\n"
                                  "  - GET /<name> Increments your counter\n"
                                  "  - GET / Displays this page\n"
                                  " Source: http://github.com/rossdylan/uvb-server\n"
                                  "----------------------------------------\n\n";
static uint64_t header_size = (sizeof(header_page)/sizeof(header_page[0])) - 1;


/**
 * Use asprintf to generate a HTTP response.
 */
char *make_http_response(int status_code, const char *status, const char *content_type, const char* response) {
    char *full_response;
    asprintf(&full_response, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n\r\n%s",
            status_code, status, content_type, strlen(response), response);
    return full_response;
}


/**
 * Unblock the given socket.
 */
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


/**
 * Create a server socket on the given port.
 * SO_REUSEPORT | SO_REUSEADDR are used
 */
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
    int reuse = 1;
    for(rp = result; rp != NULL; rp = rp->ai_next) {
        server_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if(server_fd == -1) {
            continue;
        }
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
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
    return server_fd;
}


/**
 * Check if epoll has errored.
 */
static inline bool epoll_error(struct epoll_event e) {
    return e.events & EPOLLERR || e.events & EPOLLHUP || !(e.events & EPOLLIN);
}


/**
 * Set the initial state of a connection.
 * Initialize the parser, allocate and setup the http_msg_t struct
 */
void init_connection(connection_t *session, int fd) {
    session->fd = fd;
    http_parser_init(&session->parser, HTTP_REQUEST);
    init_http_msg(&session->msg);
    session->done = false;
}


/**
 * Deallocate session structures and close the socket.
 */
void free_connection(connection_t *session) {
    close(session->fd);
    free_http_msg(&session->msg);
    // I May need to do some tear down of the parser, idk
    free(session);
}


static void configure_parser(http_parser_settings *settings) {
    settings->on_url = on_url;
    settings->on_header_field = on_header_field;
    settings->on_header_value = on_header_value;
    settings->on_headers_complete = on_headers_complete;
    settings->on_message_begin = NULL;
    settings->on_message_complete = NULL;
    settings->on_body = NULL;
}


/**
 * Function executed within a pthread to multiplex epoll acrossed threads
 * ptr is a reference to the port
 */
void *epoll_loop(void *ptr) {
    thread_data_t *data = ptr;

    int listen_fd =-1;
    struct epoll_event *events;
    struct epoll_event event;
    const char *response = make_http_response(200, "OK", "text/plain", "YOLO");
    http_parser_settings parser_settings;
    int waiting;
    connection_t *session;
    buffer_t rsp_buffer;
    buffer_init(&rsp_buffer);

    configure_parser(&parser_settings);

    listen_fd = make_server_socket(data->port);
    if((data->listen_fd = make_server_socket(data->port)) < 0) {
        perror("make_server_socket");
        return NULL;
    }
    data->listen_fd = listen_fd;

    if((data->epoll_fd = epoll_create1(0)) == -1) {
        perror("epoll_create");
        return NULL;
    }

    // set up the server sockets epoll event data
    if((event.data.ptr = malloc(sizeof(connection_t))) == NULL) {
        perror("malloc");
        return NULL;
    }
    connection_t *server_session = (connection_t *)event.data.ptr;
    server_session->fd = data->listen_fd;
    server_session->done = false;

    if(unblock_socket(data->listen_fd) == -1) {
        perror("unblock_socket");
        return NULL;
    }
    if(listen(data->listen_fd, SOMAXCONN) == -1) {
        perror("listen");
        return NULL;
    }

    event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
    if(epoll_ctl(data->epoll_fd, EPOLL_CTL_ADD, data->listen_fd, &event) == -1) {
        perror("epoll_create");
        return NULL;
    }

    if((events = calloc(MAXEVENTS, sizeof(struct epoll_event))) == NULL) {
        perror("calloc");
        return NULL;
    }

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
                continue;
            }
            /**
             * Handles the accept case, add a client new socket to epoll.
             */
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
                        if(errno != EAGAIN || errno != EWOULDBLOCK) {
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
                    if(session->msg.done) {
                        bool err = false;
#ifdef GPROF
                        if(http_url_compare(&session->msg, "/quit") == 0) {
                            exit(0);
                        }
#endif
                        if(http_url_compare(&session->msg, "/") != 0) {
                            // OH GOD DON'T LOOK I'M A HIDEOUS HACK
                            // We peak into the buffer and take away the first
                            // character in order to just get the key
                            char *key = (char *)(session->msg.url.buffer + 1);
                            // Forcefully cap out keys at 15 characters
                            // 16th char is NULL
                            if(buffer_length(&session->msg.url) > 15) {
                                session->msg.url.buffer[15] = '\0';
                            }
                            lmdb_counter_inc(data->counter, key);
                            if(send(session->fd, response, strlen(response), MSG_NOSIGNAL) == -1) {
                                err = true;
                            }
                        }
                        else {
                            buffer_append(&rsp_buffer, header_page, header_size);
                            lmdb_counter_dump(data->counter, &rsp_buffer);
                            char *resp = make_http_response(200, "OK", "text/plain", rsp_buffer.buffer);
                            if(send(session->fd, resp, strlen(resp), MSG_NOSIGNAL) == -1) {
                                err = true;
                            }
                            free(resp);
                            buffer_fast_clear(&rsp_buffer);
                        }

                        if(err) {
                            session->done = true;
                        }
                        else {
                            // We are now pipelining requests, instead of exiting
                            // out we just reinit our session state.
                            free_http_msg(&session->msg);
                            init_http_msg(&session->msg);
                            memset(&session->parser, 0, sizeof(http_parser));
                            http_parser_init(&session->parser, HTTP_REQUEST);
                            session->done = false;
                        }
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
    (void)addr;
    // set up out thread local storage for the current session
    pthread_key_create(&current_session, NULL);
    server_t *server = NULL;
    lmdb_counter_t *counter = NULL;
    if((server = malloc(sizeof(server_t))) == NULL) {
        perror("malloc");
        return NULL;
    }
    server->nthreads = nthreads;
    server->port = port;
    if((counter = lmdb_counter_init("./uvb.lmdb", nthreads)) == NULL) {
        goto new_server_free;
    }
    timer_mgr_init(&server->timers);
    register_timer(&server->timers, lmdb_counter_gen_stats, 10, (void *)counter);

    // Make our array of threads
    if((server->threads = calloc(nthreads, sizeof(pthread_t))) == NULL) {
        perror("calloc");
        server->threads = NULL;
        goto new_server_free;
    }


    cpu_set_t set;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    for(size_t i=0; i<nthreads; i++) {
        thread_data_t *tdata = NULL;
        if((tdata = malloc(sizeof(thread_data_t))) == NULL) {
            perror("malloc");
            goto new_server_free;
        }
        memset(tdata, 0, sizeof(thread_data_t));
        tdata->port = port;
        tdata->thread_id = i;
        tdata->counter = counter; // this bit is shared data

        memset(&set, 0, sizeof(cpu_set_t));
        CPU_SET(i, &set);
        pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &set);
        if(pthread_create(&server->threads[i], NULL, epoll_loop, (void *)tdata) != 0) {
            perror("pthread_create");
            goto new_server_free;
        }
    }
    goto new_server_return;

new_server_free:
    free(server->threads);
    free(server);
    server = NULL;
    if(counter != NULL) {
        lmdb_counter_destroy(counter);
    }
new_server_return:
    pthread_attr_destroy(&attr);
    return server;
}

void server_wait(server_t *server) {
    for(size_t i=0; i<server->nthreads; i++) {
        pthread_join(server->threads[i], NULL);
    }
}

int main(int argc, char *argv[]) {
    char *port = "8000";
    size_t threads = 8;
    if(argc > 1) {
        port = argv[1];
    }
    if(argc > 2) {
        errno = 0;
        threads = strtol(argv[2], NULL, 10);
        if(errno != 0) {
            perror("strtol");
            return -1;
        }
    }
    printf("Starting UVB Server on port %s with %lu threads\n", port, threads);
    server_t *server = new_server(threads, "0.0.0.0", port);
    server_wait(server);
}
