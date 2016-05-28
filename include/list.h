/**
 * File: list.h
 * A linked list implementation that I (rossdylan) wrote for a systems
 * programming course at RIT. Using it here as well.
 *
 */
#pragma once

#include <stdint.h>
#include <stdlib.h>


/**
 * Entry point for the list, holds head/tail pointers
 */
struct list_head {
    struct list_node *head;
    struct list_node *tail;
};

/**
 * Struct embedded within a user defined struct. Our linked list is
 * built from within user structures. This style is taken from the linux kernel
 */
struct list_node {
    struct list_node *next;
    struct list_node *prev;
};

/**
 * This is how the linux linkedlist impl is generic. This offsetof macro
 * is taken from ftp://rtfm.mit.edu/pub/usenet-by-group/news.answers/C-faq/faq
 * section 2.14
 */
#define offsetof(type, f) ((unsigned long)((char *)&((type *)0)->f - (char *)(type *)0))

// n: list_node
#define LIST_NEXT(n) n.next
#define LIST_PREV(n) n.prev

// n: list_node, t: type of wrapping struct
#define LIST_ENTRY(node, type) (type *)((unsigned long)(node) - offsetof(type, list))

// h: list_head; Initialize a static list_head
#define LIST_INIT(h) (h)->head = NULL; (h)->tail = NULL;



/**
 * This is kinda super ugly, but it provides a /nice/-ish api.
 * We just have to remember that there is a } needed at the end of it
 */
#define LIST_FOREACH(lhead, tmp, type) \
	tmp = LIST_ENTRY((lhead)->head, type); \
	for(struct list_node *cur=(lhead)->head; cur != NULL; cur=cur->next) { \
		tmp = LIST_ENTRY(cur, type);
//}


/**
 * Function type for a function that can be used to filter/select items
 * in a list.
 */
typedef int32_t (*list_filter_func)(struct list_node *node, void *cmpdata);

/**
 * Append a new node to the given list
 */
void list_append(struct list_head *head, struct list_node *next);

/**
 * Remove the node at the given index and return it
 */
struct list_node *list_remove(struct list_head *head, uint64_t index);

struct list_node *list_remove_by_func(struct list_head *head, list_filter_func func, void *cmpdata);

struct list_node *list_get(struct list_head *head, uint64_t index);

struct list_node *list_get_by_func(struct list_head *head, list_filter_func func, void *cmpdata);

#define LIST_GET_ENTRY(head, index, type) LIST_ENTRY(list_get(head, index), type);
