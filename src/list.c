#include "list.h"

/**
 * Get a list node by its index.
 */
struct rd_list_node *rd_list_get(struct rd_list_head *head, uint64_t index) {
    struct rd_list_node *current = head->head;

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

struct rd_list_node *rd_list_get_by_func(struct rd_list_head *head, rd_list_filter_func func, void *cmpdata) {
    struct rd_list_node *current = head->head;
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
void rd_list_append(struct rd_list_head *head, struct rd_list_node *next) {
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
struct rd_list_node *list_remove(struct rd_list_head *head, uint64_t index) {
    struct rd_list_node *node = rd_list_get(head, index);
    node->prev->next = NULL;
    return node;
}


struct rd_list_node *rd_list_remove_by_func(struct rd_list_head *head, rd_list_filter_func func, void *cmpdata) {
    struct rd_list_node *node = rd_list_get_by_func(head, func, cmpdata);
    node->prev->next = NULL;
    return node;
}
