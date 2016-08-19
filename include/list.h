/**
 * File: list.h
 * A linked list implementation that I (rossdylan) wrote for a systems
 * programming course at RIT. Using it here as well. Prefixed with rd_ because
 * bsd has sys/queue.h which redefines pretty much all of these macros.
 *
 */
#pragma once

#include <stdint.h>
#include <stdlib.h>


/**
 * Entry point for the list, holds head/tail pointers
 */
struct rd_list_head {
    struct rd_list_node *head;
    struct rd_list_node *tail;
};

/**
 * Struct embedded within a user defined struct. Our linked list is
 * built from within user structures. This style is taken from the linux kernel
 */
struct rd_list_node {
    struct rd_list_node *next;
    struct rd_list_node *prev;
};

/**
 * This is how the linux linkedlist impl is generic. This offsetof macro
 * is taken from ftp://rtfm.mit.edu/pub/usenet-by-group/news.answers/C-faq/faq
 * section 2.14
 */
#define rd_offsetof(type, f) ((unsigned long)((char *)&((type *)0)->f - (char *)(type *)0))

// n: list_node
#define RD_LIST_NEXT(n) n.next
#define RD_LIST_PREV(n) n.prev

// n: list_node, t: type of wrapping struct
#define RD_LIST_ENTRY(node, type) (type *)((unsigned long)(node) - rd_offsetof(type, list))

// h: list_head; Initialize a static list_head
#define RD_LIST_INIT(h) (h)->head = NULL; (h)->tail = NULL;



/**
 * This is kinda super ugly, but it provides a /nice/-ish api.
 * We just have to remember that there is a } needed at the end of it
 */
#define RD_LIST_FOREACH(lhead, tmp, type) \
    tmp = RD_LIST_ENTRY((lhead)->head, type); \
    for(struct rd_list_node *cur=(lhead)->head; cur != NULL; cur=cur->next) { \
        tmp = RD_LIST_ENTRY(cur, type);
//}


/**
 * Function type for a function that can be used to filter/select items
 * in a list.
 */
typedef int32_t (*rd_list_filter_func)(struct rd_list_node *node, void *cmpdata);

/**
 * Append a new node to the given list
 */
void rd_list_append(struct rd_list_head *head, struct rd_list_node *next);

/**
 * Remove the node at the given index and return it
 */
struct rd_list_node *rd_list_remove(struct rd_list_head *head, uint64_t index);

struct rd_list_node *rd_list_remove_by_func(struct rd_list_head *head, rd_list_filter_func func, void *cmpdata);

struct rd_list_node *rd_list_get(struct rd_list_head *head, uint64_t index);

struct rd_list_node *rd_list_get_by_func(struct rd_list_head *head, rd_list_filter_func func, void *cmpdata);

#define RD_LIST_GET_ENTRY(head, index, type) RD_LIST_ENTRY(rd_list_get(head, index), type);
