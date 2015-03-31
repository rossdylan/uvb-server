#include "buffer.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>


#define BUFFER_START_SIZE 32

uint64_t chkmul(uint64_t a, uint64_t b);
uint64_t chkadd(uint64_t a, uint64_t b);
uint64_t chksub(uint64_t a, uint64_t b);

uint64_t chkmul(uint64_t a, uint64_t b) {
    if(a == 0 || b == 0) {
        return 0;
    }
    if(UINT_MAX / b < a) {
        return UINT_MAX;
    }
    return a * b;
}

uint64_t chkadd(uint64_t a, uint64_t b) {
    if(UINT_MAX - b < a) {
        return UINT_MAX;
    }
    return a + b;
}

uint64_t chksub(uint64_t a, uint64_t b) {
    if(0 + b > a) {
        return 0;
    }
    return a - b;
}

int buffer_init(buffer_t *buffer) {
    if((buffer->buffer = calloc(sizeof(char), BUFFER_START_SIZE)) == NULL) {
        perror("calloc");
        return -1;
    }
    buffer->data_size = 0;
    buffer->buffer_size = BUFFER_START_SIZE;
    return 0;
}

uint64_t buffer_length(buffer_t *buffer) {
    return buffer->data_size;
}

char buffer_char_at(buffer_t *buffer, uint64_t index) {
    if(index > buffer->data_size) {
        return -1;
    }
    else {
        return buffer->buffer[index];
    }
}

uint64_t buffer_append(buffer_t *buffer, const char *string, size_t len) {
    uint64_t new_size = buffer->buffer_size;
    size_t new_data_size = chkadd(buffer->data_size, len);
    while(new_data_size >= new_size) {
        new_size = chkmul(new_size, 2);
    }
    if(buffer->buffer_size != new_size) {
        if((buffer->buffer = realloc(buffer->buffer, new_size)) == NULL) {
            perror("realloc");
            return 0;
        }
        buffer->buffer_size = new_size;
    }
    memmove(&buffer->buffer[buffer->data_size], string, len);
    buffer->data_size = new_data_size;
    return buffer->data_size;
}



int buffer_truncate(buffer_t *buffer) {
    uint64_t new_size = chkadd(buffer->data_size, 1);
    if((buffer->buffer = realloc(buffer->buffer, new_size)) == NULL) {
        perror("realloc");
        return -1;
    }
    buffer->buffer_size = new_size;
    buffer->buffer[buffer->data_size] = '\0';
    buffer->data_size = new_size;
    return 0;
}

int buffer_clear(buffer_t *buffer) {
    if((buffer->buffer = realloc(buffer->buffer, BUFFER_START_SIZE)) == NULL) {
        perror("realloc");
        return 1;
    }
    memset(buffer->buffer, 0, BUFFER_START_SIZE);
    buffer->data_size = 0;
    buffer->buffer_size = BUFFER_START_SIZE;
    return 0;
}

/**
 * Free the buffer
 * */
void buffer_free(buffer_t *buffer) {
    free(buffer->buffer);
    buffer->buffer_size = 0;
    buffer->data_size = 0;
    buffer->buffer = NULL;
}
