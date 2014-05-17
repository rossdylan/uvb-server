#include "uvbserver.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <event2/buffer.h>

/**
 * Get the number of tokens created using strtok on a string
 */
int ntok(char* str, const char* delim) {
    //XXX(rossdylan) I'm really tired, there is probably a better way to this
    char* copied = calloc(strlen(str)+1, sizeof(char));
    memmove(copied, str,  sizeof(char) * (strlen(str)+1));

    int num = 0;
    char* token = strtok(copied, delim);
    while(token != NULL) {
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
char** split(char* str, const char* delim, int n) {
    //XXX(rossdylan) I'm really tired, there is probably a better way to this
    char* copied = calloc(strlen(str)+1, sizeof(char));
    memmove(copied, str, (strlen(str)+1) * sizeof(char));

    char** tokens;
    if((tokens = calloc(n, sizeof(char*))) == NULL) {
        perror("calloc: split");
        exit(EXIT_FAILURE);
    }
    char* token = strtok(copied, delim);
    int index = 0;
    while(token != NULL) {
        size_t tsize = sizeof(char) * (strlen(token) + 1);
        if((tokens[index] = calloc(1, tsize)) == NULL) {
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
void free_split(char** s, int size) {
    for(int i=0; i<size; i++) {
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
void new_uvbserver(UVBServer* serv, struct event_base* base, char* addr, int port, CounterDB* db) {
    serv->database = db;
    serv->http = evhttp_new(base);
    evhttp_set_cb(serv->http, "/", uvb_route_display, serv->database);
    evhttp_set_gencb(serv->http, uvb_route_dispatch, serv->database);
    serv->handle = evhttp_bind_socket_with_handle(serv->http, addr, port);
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
        const struct envhttp_uri* uri = evhttp_uri_parse(strURI);
        char* path = evhttp_uri_get_path(uri);

        int nsegs = ntok(path, "/");
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
        free_split(segs, nsegs);
    }
}

void uvb_route_display(struct evhttp_request* req, void* arg) {
    CounterDB* db = (CounterDB* )arg;
    enum evhttp_cmd_type cmdtype = evhttp_request_get_command(req);
    if(cmdtype == EVHTTP_REQ_GET) {
        struct evbuffer* evb = evbuffer_new();
        int ncounters = num_counters(db);
        evbuffer_add_printf(evb, "<html>\n");
        if(ncounters > 0) {
            //XXX(rossdylan) shit man lotta calloc/free going down here
            char** names = counter_names(db);
            char* base_fmt = "<b>%s:</b> %lu <br />\n";
            for(int i=0; i<ncounters; ++i) {
                evbuffer_add_printf(evb, base_fmt, names[i], get_counter(db, names[i])->count);
            }
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
