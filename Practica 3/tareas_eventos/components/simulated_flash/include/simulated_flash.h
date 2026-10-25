#ifndef SIMULATED_FLASH_H
#define SIMULATED_FLASH_H

#include <stddef.h>

void flash_sim_init(size_t capacity_bytes);
int writeToFlash(const void *data, size_t size);
void *readFromFlash(size_t size);
size_t getDataLeft(void);

#endif
