#pragma once
#include <http_parser.h>
#include <pthread.h>
#include <stdbool.h>
#include "session.h"




// a thread local that will keep track of the currently active connection_t in order for http-parser to do its thing
// We use a constructor to initialize this

void init_http_header(http_header_t *header);
void init_http_msg(http_msg_t *msg);
void free_http_msg(http_msg_t *msg);
void free_http_header(http_header_t *header);

// Thread Local Session Management
void set_current_session(connection_t *session);
connection_t *get_current_session(void);

// callbacks for joyent's http-parser
int on_url(http_parser *_, const char *at, size_t len);
int on_header_field(http_parser *_, const char *at, size_t len);
int on_header_value(http_parser *_, const char *at, size_t len);
int on_headers_complete(http_parser *_);

// Functions to work with headers
int http_header_compare(http_msg_t *msg, const char *name, const char *value);
int http_url_compare(http_msg_t *msg, const char *value);
