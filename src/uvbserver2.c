#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include <pthread.h>
#include "uvbstore.h"
#include <stdbool.h>

#define MAXEVENTS 64

char *make_http_response(int status_code, char *status, char *content_type, char* response) {
    char *full_response;
    asprintf(&full_response, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n\r\n%s",
            status_code, status, content_type, strlen(response), response);
    return full_response;
}

typedef struct {
    int fd;
    int size;
    int max_size;
    char *buffer;
    bool done;
} SessionData;


typedef struct {
    CounterDB* database;
    char* addr;
    uint16_t port;
    pthread_t *threads;
} UVBServer;

static int listen_fd;


int make_socket_nonblocking(int socket_fd) {
    int flags;
    if((flags = fcntl(socket_fd, F_GETFL, 0)) == -1) {
        perror("fcntl");
        return -1;
    }
    flags |= O_NONBLOCK;
    if(fcntl(socket_fd, F_SETFL, flags) == -1) {
        perror("fcntl");
        return -1;
    }
    return 0;
}

int make_server_socket(char *port) {
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int s, server_fd = 0;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    s = getaddrinfo(NULL, port, &hints, &result);
    if(s != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        return -1;
    }
    for(rp = result; rp != NULL; rp = rp->ai_next) {
        server_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if(server_fd == -1) {
            continue;
        }
        s = bind(server_fd, rp->ai_addr, rp->ai_addrlen);
        if(s == 0) {
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

void *wait_loop(void *ptr) {
    int epoll_fd = (int)ptr;
    struct epoll_event *events;
    struct epoll_event event;
    char *response = make_http_response(200, "OK", "text/plain", "YOLO");
    if((events = calloc(MAXEVENTS, sizeof(struct epoll_event))) == NULL) {
        perror("failed to malloc events array");
        exit(EXIT_FAILURE);
    }
    while (1) {
        int n, i;
        n = epoll_wait(epoll_fd, events, MAXEVENTS, -1);
        if(n < 0) {
            if(errno != EINTR) {
                perror("epoll_wait");
                exit(EXIT_FAILURE);
            }
        }
        SessionData *session;
        for(i = 0; i < n; i++) {
            session = (SessionData *)events[i].data.ptr;
            if(session == NULL) {
                continue;
            }
            if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) || !(events[i].events & EPOLLIN)) {
                printf("EPOLLERR\n");
                close(session->fd);
                if(session->buffer != NULL) {
                    free(session->buffer);
                    session->buffer = NULL;
                }
                free(session);
                events[i].data.ptr = NULL;
                continue;
            }
            else if(listen_fd == session->fd) {
                struct sockaddr in_addr;
                socklen_t in_len;
                int infd, s;
                in_len = sizeof(in_addr);
                infd = accept(listen_fd, &in_addr, &in_len);
                if(infd == -1) {
                    if((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                        break;
                    }
                    else {
                        perror("Accept failed");
                        break;
                    }
                }
                if(make_socket_nonblocking(infd) == -1) {
                    exit(EXIT_FAILURE);
                }
                if((event.data.ptr = malloc(sizeof(SessionData))) == NULL) {
                    perror("new session malloc");
                    exit(EXIT_FAILURE);
                }
                SessionData *new_session = (SessionData *)event.data.ptr;
                if((new_session->buffer = calloc(512, sizeof(char))) == NULL) {
                    perror("session buffer calloc");
                    exit(EXIT_FAILURE);
                }
                new_session->fd = infd;
                new_session->size = 0;
                new_session->max_size = 512;
                new_session->done = false;
                event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                s = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, infd, &event);
                if(s == -1) {
                    perror("epoll_ctl");
                    exit(EXIT_FAILURE);
                }
                events[i].events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                if(epoll_ctl(epoll_fd, EPOLL_CTL_MOD, session->fd, &events[i]) != 0) {
                    perror("epoll_ctl EPOLL_CTL_MOD");
                    exit(EXIT_FAILURE);
                }
            }
            else {
                while (1) {
                    ssize_t count;
                    char buf[512];
                    count = read(session->fd, buf, sizeof(buf));
                    if(count == -1) {
                        if(errno != EAGAIN) {
                            perror("read");
                        }
                        break;
                    }
                    else if(count == 0) {
                        break;
                    }
                    if(count + session->size > session->max_size) {
                        /*buffer would overflow, expand to fit
                        printf("%d: Count is %lu\n", session->fd, count);
                        printf("%d: Session size is %lu\n", session->fd, session->size);
                        printf("%d: max_size is %lu\n", session->fd, session->max_size);
                        printf("%d: New buffer size: %lu\n", session->fd, session->max_size + (count * 2));
                        printf("------\n");
                        */
                        if((session->buffer = realloc((void *)session->buffer, (size_t)session->max_size + (size_t)(count * 2))) == NULL) {
                            perror("realloc");
                            exit(EXIT_FAILURE);
                        }
                        session->max_size += count*2;
                    }
                    memmove(&session->buffer[session->size], buf, (unsigned long)count);
                    session->size += count;

                    if(session->buffer[session->size-1] == '\n' && session->buffer[session->size-2] == '\r') {
                        if(session->size < session->max_size) {
                            session->buffer[session->size+1] = '\0';
                        }
                        /*
                        if(write(1, session->buffer, session->size) == -1) {
                            perror("write");
                            exit(EXIT_FAILURE);
                        }
                        */
                        if(write(session->fd, response, strlen(response)) == -1) {
                            perror("write");
                        }
                        session->done = true;
                        break;
                    }
                }
                if(session->done) {
                    if(session->buffer != NULL) {
                        free(session->buffer);
                        session->buffer = NULL;
                    }
                    close(session->fd);
                    free(session);
                    events[i].data.ptr = NULL;
                }
                else {
                    events[i].events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                    if(epoll_ctl(epoll_fd, EPOLL_CTL_MOD, session->fd, &events[i]) != 0) {
                        perror("epoll_ctl EPOLL_CTL_MOD");
                        exit(EXIT_FAILURE);
                    }
                }
            }
        }
    }
    free(events);
}

void new_uvbserver(void) {
    int num_threads = 8;
    int epoll_fd;
    struct epoll_event event;
    pthread_t threads[8];
    if((epoll_fd = epoll_create1(0)) == -1) {
        perror("epoll_create");
        exit(EXIT_FAILURE);
    }
    if((event.data.ptr = malloc(sizeof(SessionData))) == NULL) {
        perror("malloc failed for event.data");
        exit(EXIT_FAILURE);
    }
    if((listen_fd = make_server_socket("8080")) == -1) {
        perror("Failed to create server socket");
        exit(EXIT_FAILURE);
    }
    SessionData *new_session;
    if((new_session = malloc(sizeof(SessionData))) == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    new_session->fd = listen_fd;
    new_session->buffer = NULL;
    new_session->done = false;
    event.data.ptr = (void *)new_session;
    if(make_socket_nonblocking(listen_fd) == -1) {
        exit(EXIT_FAILURE);
    }
    if(listen(listen_fd, SOMAXCONN) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
    if((epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &event))  == -1) {
        perror("epoll_create");
        exit(EXIT_FAILURE);
    }
    for(int i=0;i<num_threads;i++) {
        if(pthread_create(&threads[i], NULL, wait_loop, (void *)epoll_fd)) {
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }
    }
    for(int i=0;i<num_threads;i++) {
        pthread_join(threads[i], NULL);
    }
    close(listen_fd);
}

int main(int argc, char *argv[]) {
    new_uvbserver();
}
