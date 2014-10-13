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
    int s, server_fd;
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
    return server_fd;
}

void *wait_loop(void *ptr) {
    int epoll_fd = (int)ptr;
    struct epoll_event *events;
    struct epoll_event event;
    if((events = calloc(MAXEVENTS, sizeof(struct epoll_event))) == NULL) {
        perror("failed to malloc events array");
        exit(EXIT_FAILURE);
    }
    while (1) {
        int n, i;
        if((n = epoll_wait(epoll_fd, events, MAXEVENTS, -1)) == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
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
                printf("Accepting shit\n");
                struct sockaddr in_addr;
                socklen_t in_len;
                int infd, s;
                char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
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
                s = getnameinfo(&in_addr, in_len, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV);
                if(s == 0) {
                    printf("Accepted connection on fd %d (host=%s, port=%s)\n",  infd, hbuf, sbuf);
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
                printf("Reading some shit\n");
                int done = 0;
                while (1) {
                    ssize_t count;
                    char buf[512];
                    count = read(session->fd, buf, sizeof(buf));
                    if(count == -1) {
                        if(errno != EAGAIN) {
                            perror("read");
                            done = 1;
                        }
                        break;
                    }
                    else if(count == 0) {
                        done = 1;
                        break;
                    }
                    if(count + session->size > session->max_size) {
                        //buffer would overflow, expand to fit
                        if((session->buffer = realloc((void *)session->buffer, sizeof(session->buffer) + (count * 2))) == NULL) {
                            perror("realloc");
                            exit(EXIT_FAILURE);
                        }
                        session->max_size += count*2;
                    }
                    memmove(&session->buffer[session->size], buf, count);
                    session->size += count;

                    if(session->buffer[session->size-1] == '\n' && session->buffer[session->size-2] == '\r') {
                        if(session->size < session->max_size) {
                            session->buffer[session->size+1] = '\0';
                        }
                        if(write(1, session->buffer, session->size) == -1) {
                            perror("write");
                            exit(EXIT_FAILURE);
                        }
                        char *response = make_http_response(200, "OK", "text/plain", "YOLO");
                        write(session->fd, response, strlen(response));
                        free(response);
                        done = 1;
                        break;
                    }
                    events[i].events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                    if(epoll_ctl(epoll_fd, EPOLL_CTL_MOD, session->fd, &events[i]) != 0) {
                        perror("epoll_ctl EPOLL_CTL_MOD");
                        exit(EXIT_FAILURE);
                    }
                }
                if(done) {
                    printf("Closed connection on fd %d\n", session->fd);
                    if(session->buffer != NULL) {
                        free(session->buffer);
                        session->buffer = NULL;
                    }
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, session->fd, NULL);
                    close(session->fd);
                    free(session);
                    events[i].data.ptr = NULL;
                }
            }
        }
    }
    free(events);
}

void new_uvbserver(void) {
    int num_threads;
    int epoll_fd;
    struct epoll_event event;
    pthread_t thread1, thread2;
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
    if(pthread_create(&thread1, NULL, wait_loop, (void *)epoll_fd)) {
        perror("pthread_create");
        exit(EXIT_FAILURE);
    }
    if(pthread_create(&thread2, NULL, wait_loop, (void *)epoll_fd)) {
        perror("pthread_create");
        exit(EXIT_FAILURE);
    }
    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);
    close(listen_fd);
}

int main(int argc, char *argv[]) {
    new_uvbserver();
}
