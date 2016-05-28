#include "list.h"

/**
 * Get a list node by its index.
 */
struct list_node *list_get(struct list_head *head, uint64_t index) {
    struct list_node *current = head->head;

    uint64_t cindex = 0;
    while(1) {
       if(current == NULL || index == cindex) {
           break;
       }
       current = current->next;
       cindex++;
    }
    return current;
}

struct list_node *list_get_by_func(struct list_head *head, list_filter_func func, void *cmpdata) {
    struct list_node *current = head->head;
    uint64_t cindex = 0;
    while(1) {
        if(current == NULL || func(current, cmpdata) == 1) {
            break;
        }
        current = current->next;
        cindex++;
    }
    return current;
}


/**
 * Append a new node to the given list
 */
void list_append(struct list_head *head, struct list_node *next) {
    // Empty list case
    if(head->head == NULL) {
        head->head = next;
        head->tail = next;
        next->next = NULL;
        next->prev = NULL;
    }
    else {
        head->tail->next = next;
        next->next = NULL;
        next->prev = head->tail;
        head->tail = next;
    }
}

/**
 * Remove the node at the given index and return it
 */
struct list_node *list_remove(struct list_head *head, uint64_t index) {
	struct list_node *node = list_get(head, index);
	node->prev->next = NULL;
	return node;
}


struct list_node *list_remove_by_func(struct list_head *head, list_filter_func func, void *cmpdata) {
	struct list_node *node = list_get_by_func(head, func, cmpdata);
	node->prev->next = NULL;
	return node;
}
