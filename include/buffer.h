#pragma once
#include <stdint.h>
#include <stdlib.h>


typedef struct {
    char *buffer;
    uint64_t data_size;
    uint64_t buffer_size;
} buffer_t;


int buffer_init(buffer_t *buffer);
/**
 * Truncate the empty space in a buffer and turn it into a string
 */
int buffer_truncate(buffer_t *buffer);

/**
 * Append a string to the buffer and return the new size
 * */
uint64_t buffer_append(buffer_t *buffer, const char *string, size_t len);

/**
 * Free the buffer
 * */
void buffer_free(buffer_t *buffer);

int buffer_clear(buffer_t *buffer);
uint64_t buffer_length(buffer_t *buffer);
char buffer_char_at(buffer_t *buffer, uint64_t index);
