#ifndef READER_H
#define READER_H

#include <stdint.h>

uint8_t* debug_read_bytes(uint64_t address, uint32_t bytes, uint8_t *buffer);

#endif
