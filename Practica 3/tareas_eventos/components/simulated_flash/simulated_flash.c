#include "simulated_flash.h"

#include <stdlib.h>
#include <string.h>

static unsigned char *buffer;
static unsigned char *read_buffer;
static size_t capacity;
static size_t head;
static size_t used;

void flash_sim_init(size_t capacity_bytes)
{
    free(buffer);
    free(read_buffer);
    buffer = capacity_bytes == 0 ? NULL : malloc(capacity_bytes);
    read_buffer = capacity_bytes == 0 ? NULL : malloc(capacity_bytes);
    capacity = buffer != NULL && read_buffer != NULL ? capacity_bytes : 0;
    if (capacity == 0) {
        free(buffer);
        free(read_buffer);
        buffer = NULL;
        read_buffer = NULL;
    }
    head = 0;
    used = 0;
}

int writeToFlash(const void *data, size_t size)
{
    if (data == NULL || size == 0 || buffer == NULL || size > capacity - used) {
        return -1;
    }

    size_t tail = (head + used) % capacity;
    size_t first_part = size < capacity - tail ? size : capacity - tail;
    memcpy(buffer + tail, data, first_part);
    memcpy(buffer, (const unsigned char *)data + first_part, size - first_part);
    used += size;
    return 0;
}

void *readFromFlash(size_t size)
{
    if (size == 0 || size > used || read_buffer == NULL) {
        return NULL;
    }

    size_t first_part = size < capacity - head ? size : capacity - head;
    memcpy(read_buffer, buffer + head, first_part);
    memcpy(read_buffer + first_part, buffer, size - first_part);
    head = (head + size) % capacity;
    used -= size;
    return read_buffer;
}

size_t getDataLeft(void)
{
    return used;
}
