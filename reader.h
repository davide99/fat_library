#ifndef READER_H
#define READER_H

#include <stdint.h>

void *debug_read_bytes(uint64_t address, uint32_t bytes, void *buffer);

#endif
