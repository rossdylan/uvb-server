#include "uvbserver.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <event2/buffer.h>

/**
 * Get the number of tokens created using strtok on a string
 */


uint64_t ntok(char* str, const char* delim) {
    //XXX(rossdylan) I'm really tired, there is probably a better way to this
    size_t size = strlen(str)+1;
    char* copied;
    if((copied = calloc(size, sizeof(char))) == NULL) {
        perror("calloc: ntok");
        exit(EXIT_FAILURE);
    }
    memmove(copied, str,  sizeof(char) * size);

    uint64_t num = 0;
    char* token = strtok(copied, delim);
    while(token != NULL) { 
        if(UINT64_MAX - 1 < num) {
            fprintf(stderr, "Some one tried to overflow tokenizer\n");
            num = 0;
            break;
        }
        num++;
        token = strtok(NULL, delim);
    }
    free(copied);
    copied = NULL;
    return num;
}

/**
 * Make an array of strings holding all tokens in a string
 */
char** split(char* str, const char* delim, uint64_t n) {
    //XXX(rossdylan) I'm really tired, there is probably a better way to this
    size_t size = strlen(str)+1;
    char* copied;
    if((copied = calloc(size, sizeof(char))) == NULL) {
        perror("calloc: ntok");
        exit(EXIT_FAILURE);
    }
    memmove(copied, str,  sizeof(char) * size);

    char** tokens;
    if((tokens = calloc(n, sizeof(char*))) == NULL) {
        perror("calloc: split");
        exit(EXIT_FAILURE);
    }
    char* token = strtok(copied, delim);
    int index = 0;
    while(token != NULL) {
        size_t tsize = (strlen(token) + 1);
        if((tokens[index] = calloc(tsize, sizeof(char))) == NULL) {
            perror("calloc: split: token");
            exit(EXIT_FAILURE);
        }
        memmove(tokens[index], token, tsize);
        index++;
        token = strtok(NULL, delim);
    }
    free(copied);
    copied = NULL;
    return tokens;
}

/**
 * Free an array of strings of the given size
 */
void free_split(char** s, uint64_t size) {
    for(uint64_t i=0; i<size; i++) {
        free(s[i]);
        s[i] = NULL;
    }
    free(s);
    s = NULL;
}

/**
 * Take a freshly allocated UVBServer struct and fill it up and set all the values to their defaults
 * This includes:
 *  making the evhttp struct
 *  setting the default route handler to uvb_unknown_route
 *  tell evhttp where to listen
 */
void new_uvbserver(UVBServer* serv, struct event_base* base, char* addr, uint16_t port, CounterDB* db) {
    serv->database = db;
    serv->http = evhttp_new(base);
    evhttp_set_cb(serv->http, "/", uvb_route_display, serv->database);
    evhttp_set_gencb(serv->http, uvb_route_dispatch, serv->database);
    serv->handle = evhttp_bind_socket_with_handle(serv->http, addr, port);
    struct timeval rps_timer = {1, 0};
    struct event* rpsevent = event_new(base, -1, EV_PERSIST, calculate_rps, db);
    event_add(rpsevent, &rps_timer);
    if(!serv->handle) {
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


void uvb_route_dispatch(struct evhttp_request* req, void* arg) {
    CounterDB* db = (CounterDB* )arg;
    enum evhttp_cmd_type cmdtype = evhttp_request_get_command(req);
    if(cmdtype == EVHTTP_REQ_POST) {
        const char* strURI = evhttp_request_get_uri(req);
        struct evhttp_uri* uri;
        if((uri = evhttp_uri_parse(strURI)) == NULL) {
            evhttp_send_reply(req, 500, "Bad URI", NULL);
            return;
        }
        char* path = evhttp_decode_uri((char* )evhttp_uri_get_path(uri));
        uint64_t nsegs = ntok(path, "/");
        //given a weird address or ntok was overflowed
        if(nsegs == 0) {
            evhttp_send_reply(req, 500, "Internal Server Error", NULL);
            evhttp_uri_free(uri);
            uri = NULL;
            return;
        }
        char** segs = split(path, "/", nsegs);
        // this is most likely /<username>
        if(nsegs == 1) {
            if(counter_exists(db, segs[0])) {
                increment_counter(db, segs[0]);
                evhttp_send_reply(req, 200, "OK", NULL);
            }
            else {
                evhttp_send_reply(req, 404, "User not Found", NULL);
            }
        }
        else {
            // handle /register/<username>
            if(nsegs == 2) {
                if(strcmp(segs[0], "register") == 0) {
                    if(!counter_exists(db, segs[1])) {
                        fprintf(stderr, "Created new counter for %s\n", segs[1]);
                        add_counter(db, segs[1]);
                        evhttp_send_reply(req, 201, "User Created", NULL);
                    }
                    else {
                        evhttp_send_reply(req, 400, "User Already Exists", NULL);
                    }
                }
            }
            // handle catch all
            else {
                evhttp_send_reply(req, 404, "Not Found", NULL);
            }
        }
        evhttp_uri_free(uri);
        uri = NULL;
        free_split(segs, nsegs);
    }
}

void uvb_route_display(struct evhttp_request* req, void* arg) {
    CounterDB* db = (CounterDB* )arg;
    enum evhttp_cmd_type cmdtype = evhttp_request_get_command(req);
    if(cmdtype == EVHTTP_REQ_GET) {
        struct evbuffer* evb = evbuffer_new();
        uint64_t ncounters = num_counters(db);
        evbuffer_add_printf(evb, "<html>\
                <title> Welcome to Ultimate Victory Battle </title>\
                <p>\n\
                This is the new UVB server, written in C by rossdylan.\n\
                <br />\
                To play POST to /register/[yourname]\n\
                <br />\
                Then POST to /[yourname] to increment your count\n\
                <br />\
                Counters are displayed here. Have fun\n\
                </p><br />");
        if(ncounters > 0) {
            //XXX(rossdylan) shit man lotta calloc/free going down here
            //TODO(rossdylan) I need to compare the ways I can get a array of names
            //one goes to disk with NamesDB and one rips them out of the GHashTable
            char** names = counter_names(db);
            uint64_t topCounter = 0;
            const char* topName = "";
            for(uint64_t i=0; i<ncounters; ++i) {
                Counter* counter = get_counter(db, names[i]);
                if(counter->count > topCounter) {
                    topName = names[i];
                    topCounter = counter->count;
                }
                evbuffer_add_printf(evb, "<b>%s:</b> %lu - %lu req/s <br />\n", names[i], counter->count, counter->rps);
            }
            evbuffer_add_printf(evb, "Current Winner is: <b>%s</b><br />\n", topName);
            free_names(names, ncounters);
        }
        evbuffer_add_printf(evb, "</html>\n");
        evhttp_send_reply(req, 200, "OK", evb);
        evbuffer_free(evb);
    }
    else {
        evhttp_send_reply(req, 403, "Not Allowed", NULL);
    }
}

void calculate_rps(int fd, short event, void *arg) {
    CounterDB* db = (CounterDB* )arg;
    uint64_t ncounters = num_counters(db);
    char** names = counter_names(db);
    for(uint64_t i=0; i<ncounters; i++) {
        Counter* counter = get_counter(db, names[i]);
        counter->rps = counter->count - counter->rps_prevcount;
        counter->rps_prevcount = counter->count;
    }
    free_names(names, ncounters);
}
