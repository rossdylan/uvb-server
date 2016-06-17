#define _GNU_SOURCE

#include "server.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include "uvbloop.h"

#ifdef __LINUX__
#include <sched.h>
#endif

#ifdef __FreeBSD__
#include <sys/param.h>
#include <sys/cpuset.h>
#include <pthread_np.h>
#endif


static const char header_page[] = "--- Ultimate Victory Battle (v4.0.0) ---\n"
                                  " Rules: \n"
                                  "  - Increment your counter higher/faster than everyone else\n"
                                  "  - GET /<name> Increments your counter\n"
                                  "  - GET / Displays this page\n"
                                  " Source: http://github.com/rossdylan/uvb-server\n"
                                  "----------------------------------------\n\n";
static uint64_t header_size = (sizeof(header_page)/sizeof(header_page[0])) - 1;

static counter_t *counter;
static char *inc_response;
static uint64_t inc_response_sz;

/**
 * Use asprintf to generate a HTTP response.
 */
int make_http_response(char **resp_ptr, int status_code, const char *status, const char *content_type, const char* response) {
    return asprintf(resp_ptr, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n\r\n%s",
            status_code, status, content_type, strlen(response), response);
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
 * Set the initial state of a connection.
 * Initialize the parser, allocate and setup the http_msg_t struct
 */
void init_connection(connection_t *session, int fd) {
    session->fd = fd;
    http_parser_init(&session->parser, HTTP_REQUEST);
    session->parser.data = session;
    init_http_msg(&session->msg);
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

__thread buffer_t rsp_buffer;

static int on_message_complete(http_parser *hp) {
    connection_t *session = hp->data;

#ifdef GPROF
    if(http_url_compare(&session->msg, "/quit") == 0) {
        printf("Exit requested...\n");
        exit(0);
    }
#endif

#if defined(MSG_NOSIGNAL)
    int send_flags = MSG_NOSIGNAL;
#else
    int send_flags = 0;
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
        counter_inc(counter, key);
        send(session->fd, inc_response, inc_response_sz, send_flags);
    }
    else {
        buffer_append(&rsp_buffer, header_page, header_size);
        counter_dump(counter, &rsp_buffer);
        char *resp = NULL;
        int len = make_http_response(&resp, 200, "OK", "text/plain", rsp_buffer.buffer);

        send(session->fd, resp, len, send_flags);

        free(resp);
        buffer_fast_clear(&rsp_buffer);
    }

    free_http_msg(&session->msg);
    init_http_msg(&session->msg);

    return 0;
}


static void configure_parser(http_parser_settings *settings) {
    settings->on_url = on_url;
#ifdef UVB_PARSE_HEADERS
    settings->on_header_field = on_header_field;
    settings->on_header_value = on_header_value;
#else
    settings->on_header_field = NULL;
    settings->on_header_value = NULL;
#endif
    settings->on_headers_complete = on_headers_complete;
    settings->on_message_begin = NULL;
    settings->on_message_complete = on_message_complete;
    settings->on_body = NULL;
}

/**
 * Function executed within a pthread to multiplex epoll acrossed threads
 * ptr is a reference to the port
 */
void *epoll_loop(void *ptr) {
    thread_data_t *data = ptr;

    int listen_fd =-1;

    uvbloop_t *loop = NULL;
    if((loop = uvbloop_init(NULL)) == NULL) {
        perror("uvbloop_init");
        return NULL;
    }
    uvbloop_event_t *events;

    http_parser_settings parser_settings;
    int waiting;
    connection_t *session;
    buffer_init(&rsp_buffer);

    configure_parser(&parser_settings);

    listen_fd = make_server_socket(data->port);
    if((data->listen_fd = make_server_socket(data->port)) < 0) {
        perror("make_server_socket");
        return NULL;
    }
    data->listen_fd = listen_fd;

    connection_t *server_session = NULL;
    if((server_session = malloc(sizeof(connection_t))) == NULL) {
        perror("malloc");
        return NULL;
    }
    server_session->fd = data->listen_fd;

    if(unblock_socket(data->listen_fd) == -1) {
        perror("unblock_socket");
        return NULL;
    }
    if(listen(data->listen_fd, SOMAXCONN) == -1) {
        perror("listen");
        return NULL;
    }

    if(uvbloop_register_fd(loop, data->listen_fd, (void *)server_session, UVBLOOP_R) == -1) {
        perror("uvbloop_register_fd");
        return NULL;
    }

    if((events = calloc(MAXEVENTS, sizeof(uvbloop_event_t))) == NULL) {
        perror("calloc");
        return NULL;
    }

    while(true) {
        waiting = uvbloop_wait(loop, events, MAXEVENTS);
        if(waiting < 0) {
            if(errno != EINTR) {
                perror("uvbloop_wait");
                return NULL;
            }
        }
        for(int i=0; i<waiting; i++) {
            session = (connection_t *)uvbloop_event_data(&events[i]);
            if(session == NULL) {
                continue;
            }
            if(uvbloop_event_error(&events[i])) {
                free_connection(session);
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
                        goto loop_accept_failed;
                    }
                    else {
                        perror("accept");
                        goto loop_accept_failed;
                    }
                }

                if(unblock_socket(in_fd) == -1) {
                    //TODO maybe do extra handling of this error case?
                    perror("unblock_socket");
                    goto loop_accept_failed;
                }
#if defined(SO_NOSIGPIPE)
                int set = 1;
                setsockopt(in_fd, SOL_SOCKET, SO_NOSIGPIPE, &set, sizeof(set));
#endif
                connection_t *new_session = NULL;
                if((new_session = malloc(sizeof(connection_t))) == NULL) {
                    perror("malloc");
                    goto loop_accept_failed;
                }
                init_connection(new_session, in_fd);
                if(uvbloop_register_fd(loop, in_fd, (void *)new_session, UVBLOOP_R) == -1) {
                    perror("uvbloop_register_fd");
                    free_connection(new_session);
                    goto loop_accept_failed;
                }

loop_accept_failed: ;
            }
            else {
                bool done = false;

                char buf[4096];
                ssize_t count = -1;
                if((count = read(session->fd, buf, sizeof(buf))) == -1) {
                    if(errno != EAGAIN && errno != EWOULDBLOCK) {
                        done = true;
                    }
                    goto serviced;
                } else if(count == 0) {
                    // EOF
                    done = true;
                    goto serviced;
                }

                // Since we check if count is -1 and back out
                // before this point this cast should be safe
                size_t parsed = http_parser_execute(
                    &session->parser, &parser_settings, buf, (size_t)count);

                if(parsed != (size_t)count) {
                    // ERROR OH NO
                    done = true;
                    // goto serviced;
                }
serviced:
                if(done) {
                    free_connection(session);
                }
            }
        }
    }
}

server_t *new_server(const size_t nthreads, const char *addr, const char *port) {
    (void)addr;
    // create the standard response for increments
    inc_response_sz = make_http_response(&inc_response, 200, "OK", "text/plain", "YOLO");
    server_t *server = NULL;
    if((server = malloc(sizeof(server_t))) == NULL) {
        perror("malloc");
        return NULL;
    }
    server->nthreads = nthreads;
    server->port = port;
    if((counter = counter_init("./uvb.lmdb", nthreads)) == NULL) {
        goto new_server_free;
    }
    timer_mgr_init(&server->timers);
    register_timer(&server->timers, counter_gen_stats, STATS_SECS * 1000, (void *)counter);

    // Make our array of threads
    if((server->threads = calloc(nthreads, sizeof(pthread_t))) == NULL) {
        perror("calloc");
        server->threads = NULL;
        goto new_server_free;
    }

#ifdef __linux__
    cpu_set_t set;
    size_t set_sz = sizeof(cpu_set_t);
#endif
#ifdef __FreeBSD__
    cpuset_t set;
    size_t set_sz = sizeof(cpuset_t);
#endif

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

#ifndef __APPLE__
        memset(&set, 0, set_sz);
        CPU_SET(i, &set);
        pthread_attr_setaffinity_np(&attr, set_sz, &set);
#endif
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
        counter_destroy(counter);
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
