#include "http.h"
#include <string.h>

void init_http_header(http_header_t *header) {
    buffer_init(&header->name);
    buffer_init(&header->value);
}

void init_http_msg(http_msg_t *msg) {
    msg->current_header = 0;
    msg->header_ready = false;
    msg->reading_value = false;
    memset(&msg->headers, 0, sizeof(msg->headers));
    buffer_init(&msg->url);
    msg->done = false;
}

void free_http_header(http_header_t *header) {
    buffer_free(&header->name);
    buffer_free(&header->value);
}

void free_http_msg(http_msg_t *msg) {
    if(msg->done == false && msg->current_header == 0) {
        // we got some weird ass invalid request...
        goto free_http_msg_url_free;
    }
    for(uint64_t i=0; i<=msg->current_header; i++) {
        free_http_header(&msg->headers[i]);
    }
free_http_msg_url_free:
    buffer_free(&msg->url);
}

int on_url(http_parser *hp, const char *at, size_t len) {
    connection_t *session = hp->data;
    buffer_append(&session->msg.url, at, len);
    return 0;
}

int on_header_field(http_parser *hp, const char *at, size_t len) {
    connection_t *session = hp->data;
    if(session->msg.reading_value) {
        session->msg.current_header++;
        session->msg.reading_value = false;
        session->msg.header_ready = false;
    }
    uint64_t current_header = session->msg.current_header;
    if(!session->msg.header_ready) {
        init_http_header(&session->msg.headers[current_header]);
        session->msg.header_ready = true;
    }
    buffer_append(&session->msg.headers[current_header].name, at, len);
    return 0;
}

int on_header_value(http_parser *hp, const char *at, size_t len) {
    connection_t *session = hp->data;
    session->msg.reading_value = true;
    uint64_t current_header = session->msg.current_header;
    buffer_append(&session->msg.headers[current_header].value, at, len);
    return 0;
}

int on_headers_complete(http_parser *hp) {
    connection_t *session = hp->data;
    session->msg.done = true;
    return 0;
}

int http_header_compare(http_msg_t *msg, const char *name, const char *value) {
    (void)msg;
    (void)name;
    (void)value;
    return -1;
}

int http_url_compare(http_msg_t *msg, const char *value) {
    if (strlen(value) != buffer_length(&msg->url)) return 1;
    int foo = strncmp(msg->url.buffer, value, buffer_length(&msg->url));
    return foo;
}
